/*
Instant Record
Copyright (C) 2026 Rafael Roman <support@instanrp.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <util/platform.h>
#include "source-record-filter.h"

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>

#include <string>
#include <vector>
#include <cstring>
#include <functional>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QFileDialog>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGraphicsDropShadowEffect>

/* Instant Replay brand palette. */
#define IR_BG "#000000"
#define IR_CARD "#0d0d0f"
#define IR_RED "#e0403a"
#define IR_GOLD "#f5c04a"
#define IR_BLUE "#2f8fe0"
#define IR_TEXT "#f4f6f8"
#define IR_MUTED "#7c848c"

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QString format_elapsed(int64_t ns)
{
	if (ns <= 0)
		return QStringLiteral("--");
	qint64 s = ns / 1000000000LL;
	return QString::asprintf("%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
}

static std::string sr_cfg_file()
{
	char *dir = obs_module_get_config_path(obs_current_module(), "");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	char *f = obs_module_get_config_path(obs_current_module(), "global.json");
	std::string s = f ? f : "";
	bfree(f);
	return s;
}

static void selectByData(QComboBox *c, const QVariant &v)
{
	int i = c->findData(v);
	if (i >= 0)
		c->setCurrentIndex(i);
}

static uint32_t qtToObsMods(Qt::KeyboardModifiers m)
{
	uint32_t r = 0;
	if (m & Qt::ShiftModifier)
		r |= INTERACT_SHIFT_KEY;
	if (m & Qt::ControlModifier)
		r |= INTERACT_CONTROL_KEY;
	if (m & Qt::AltModifier)
		r |= INTERACT_ALT_KEY;
	if (m & Qt::MetaModifier)
		r |= INTERACT_COMMAND_KEY;
	return r;
}

/* True if the source already carries an Instant Record filter. */
static bool sr_source_has_filter(obs_source_t *src)
{
	bool found = false;
	obs_source_enum_filters(
		src,
		[](obs_source_t *, obs_source_t *filter, void *d) {
			const char *id = obs_source_get_id(filter);
			if (id && strcmp(id, "instant_record_filter") == 0)
				*(bool *)d = true;
		},
		&found);
	return found;
}

/* Collect names of video sources that don't have the filter yet. */
static bool sr_collect_sources(void *data, obs_source_t *src)
{
	auto *names = static_cast<std::vector<QString> *>(data);
	uint32_t flags = obs_source_get_output_flags(src);
	if (!(flags & OBS_SOURCE_VIDEO))
		return true;
	enum obs_source_type t = obs_source_get_type(src);
	if (t == OBS_SOURCE_TYPE_FILTER || t == OBS_SOURCE_TYPE_TRANSITION)
		return true;
	if (sr_source_has_filter(src))
		return true;
	const char *nm = obs_source_get_name(src);
	if (nm && *nm)
		names->push_back(QString::fromUtf8(nm));
	return true;
}

/* Create and attach an Instant Record filter to a source by name. */
static void sr_add_filter_to_source(const QString &name)
{
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (!src)
		return;
	obs_source_t *flt = obs_source_create_private("instant_record_filter", "Instant Record", nullptr);
	if (flt) {
		obs_source_filter_add(src, flt);
		obs_source_release(flt);
	}
	obs_source_release(src);
}

/* A button that captures the next key press and reports it as an OBS
 * key + modifiers. No Q_OBJECT needed — it uses a std::function. */
class HotkeyButton : public QPushButton {
public:
	std::function<void(uint32_t, int)> onCapture;
	bool capturing = false;
	explicit HotkeyButton(QWidget *p = nullptr) : QPushButton(p) { setFocusPolicy(Qt::StrongFocus); }
	void startCapture()
	{
		capturing = true;
		setText(QStringLiteral("…"));
		setFocus();
		grabKeyboard();
	}

protected:
	void keyPressEvent(QKeyEvent *e) override
	{
		if (!capturing) {
			QPushButton::keyPressEvent(e);
			return;
		}
		int k = e->key();
		if (k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta ||
		    k == Qt::Key_AltGr)
			return; /* wait for a real key */
		capturing = false;
		releaseKeyboard();
		if (k == Qt::Key_Escape) {
			if (onCapture)
				onCapture(0, 0); /* clear */
			return;
		}
		int obskey = (int)obs_key_from_virtual_key((int)e->nativeVirtualKey());
		if (onCapture)
			onCapture(qtToObsMods(e->modifiers()), obskey);
	}
	void focusOutEvent(QFocusEvent *e) override
	{
		if (capturing) {
			capturing = false;
			releaseKeyboard();
		}
		QPushButton::focusOutEvent(e);
	}
};

struct Card {
	QFrame *w;
	QLabel *dot;
	QLabel *status;
	QLabel *time;
	QPushButton *clip;
	int index;
};

class InstantRecordDock : public QWidget {
public:
	explicit InstantRecordDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		setStyleSheet(
			"QWidget{background:" IR_BG ";color:" IR_TEXT ";font-size:12px;}"
			"QLabel{background:transparent;}"
			"QLabel#title{font-size:15px;font-weight:800;}"
			"QLabel#subtitle{font-size:10px;font-weight:600;color:" IR_GOLD ";}"
			"QFrame#card{background:" IR_CARD ";border:1px solid #17171b;border-radius:12px;}"
			"QPushButton{background:#161618;color:" IR_TEXT
			";border:1px solid #2a2a2e;border-radius:14px;padding:8px 14px;font-weight:700;}"
			"QPushButton:hover{border-color:" IR_GOLD ";}"
			"QPushButton#start{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f0564f,stop:1 #c9322c);border:none;color:#fff;}"
			"QPushButton#save{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd27a,stop:1 #e9a92f);border:none;color:#3a2a08;}"
			"QPushButton#clip{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4aa6f0,stop:1 #2477c8);border:none;color:#fff;border-radius:13px;padding:5px 14px;}"
			"QPushButton#hk{background:transparent;border:none;border-radius:10px;padding:4px 10px;font-size:11px;font-weight:700;color:#8b939b;}"
			"QPushButton#hk:hover{background:#1c1c20;color:" IR_GOLD ";}");

		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(10, 10, 10, 10);
		root->setSpacing(8);

		/* Header: logo + name + counters. */
		auto *header = new QHBoxLayout();
		auto *logo = new QLabel(this);
		char *logoPath = obs_module_file("locale/logo.png");
		QPixmap pix;
		if (logoPath) {
			pix.load(QString::fromUtf8(logoPath));
			bfree(logoPath);
		}
		if (!pix.isNull())
			logo->setPixmap(pix.scaled(30, 30, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		header->addWidget(logo);
		auto *titleBox = new QVBoxLayout();
		titleBox->setSpacing(0);
		auto *title = new QLabel("Instant Record", this);
		title->setObjectName("title");
		auto *subtitle = new QLabel("by Instant Replay", this);
		subtitle->setObjectName("subtitle");
		titleBox->addWidget(title);
		titleBox->addWidget(subtitle);
		header->addLayout(titleBox);
		header->addStretch();
		counters = new QLabel(this);
		counters->setStyleSheet("font-size:11px;font-weight:700;");
		header->addWidget(counters);
		root->addLayout(header);

		/* Scrollable-ish card container. */
		cardsBox = new QVBoxLayout();
		cardsBox->setSpacing(6);
		root->addLayout(cardsBox);
		root->addStretch();

		/* Bottom action bar. */
		auto *btns = new QHBoxLayout();
		auto *startAll = new QPushButton(T("InstantRecord.Dock.StartAll"), this);
		startAll->setObjectName("start");
		shadow(startAll);
		auto *stopAll = new QPushButton(T("InstantRecord.Dock.StopAll"), this);
		auto *saveAll = new QPushButton(T("InstantRecord.Dock.SaveAll"), this);
		saveAll->setObjectName("save");
		shadow(saveAll);
		auto *config = new QPushButton(T("InstantRecord.Dock.GlobalConfig"), this);
		auto *addCams = new QPushButton(T("InstantRecord.Dock.AddCams"), this);
		btns->addWidget(startAll);
		btns->addWidget(stopAll);
		btns->addWidget(saveAll);
		btns->addStretch();
		btns->addWidget(addCams);
		btns->addWidget(config);
		root->addLayout(btns);

		connect(startAll, &QPushButton::clicked, this, [] { sr_registry_start_all(); });
		connect(stopAll, &QPushButton::clicked, this, [] { sr_registry_stop_all(); });
		connect(saveAll, &QPushButton::clicked, this, [] { sr_registry_save_all(); });
		connect(addCams, &QPushButton::clicked, this, [this] { pickAndAddCameras(); });
		connect(config, &QPushButton::clicked, this, [this] { openGlobalConfig(); });

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(500);
		refresh();
	}

private:
	QVBoxLayout *cardsBox;
	QLabel *counters;
	std::vector<Card> cards;
	QString lastSig;

	static void shadow(QWidget *w)
	{
		auto *e = new QGraphicsDropShadowEffect(w);
		e->setBlurRadius(12);
		e->setOffset(0, 3);
		e->setColor(QColor(0, 0, 0, 150));
		w->setGraphicsEffect(e);
	}

	HotkeyButton *makeHk(const char *glyph, int camIndex, int which)
	{
		auto *b = new HotkeyButton();
		b->setObjectName("hk");
		QString g = QString::fromUtf8(glyph);
		char cur[128];
		sr_registry_hotkey_str((size_t)camIndex, which, cur, sizeof(cur));
		b->setText(g + " " + (cur[0] ? QString::fromUtf8(cur) : QStringLiteral("—")));
		connect(b, &QPushButton::clicked, b, [b] { b->startCapture(); });
		b->onCapture = [b, camIndex, which, g](uint32_t mods, int key) {
			sr_registry_bind_hotkey((size_t)camIndex, which, mods, key);
			char cur2[128];
			sr_registry_hotkey_str((size_t)camIndex, which, cur2, sizeof(cur2));
			b->setText(g + " " + (cur2[0] ? QString::fromUtf8(cur2) : QStringLiteral("—")));
		};
		return b;
	}

	void rebuildCards(struct sr_status_row *rows, size_t n)
	{
		/* Clear old cards. */
		for (auto &c : cards)
			c.w->deleteLater();
		cards.clear();
		QLayoutItem *item;
		while ((item = cardsBox->takeAt(0)) != nullptr)
			delete item;

		for (int i = 0; i < (int)n; i++) {
			auto *card = new QFrame();
			card->setObjectName("card");
			auto *cl = new QVBoxLayout(card);
			cl->setContentsMargins(10, 8, 10, 8);
			cl->setSpacing(6);

			auto *r1 = new QHBoxLayout();
			auto *dot = new QLabel("●");
			auto *nm = new QLabel(QString("%1\n%2x%3 · %4")
						      .arg(rows[i].name)
						      .arg(rows[i].width)
						      .arg(rows[i].height)
						      .arg(rows[i].format));
			nm->setStyleSheet("font-weight:600;");
			auto *stt = new QLabel();
			stt->setStyleSheet("font-weight:800;");
			auto *tm = new QLabel();
			tm->setStyleSheet("color:#c9d0d6;");
			r1->addWidget(dot);
			r1->addWidget(nm);
			r1->addStretch();
			r1->addWidget(stt);
			r1->addSpacing(8);
			r1->addWidget(tm);
			cl->addLayout(r1);

			auto *r2 = new QHBoxLayout();
			r2->addWidget(makeHk("\xE2\x96\xB6", i, SR_HK_START)); /* ▶ */
			r2->addWidget(makeHk("\xE2\x96\xA0", i, SR_HK_STOP));  /* ■ */
			r2->addWidget(makeHk("\xE2\x98\x85", i, SR_HK_SAVE));  /* ★ */
			r2->addStretch();
			auto *clip = new QPushButton(T("InstantRecord.Dock.Save"));
			clip->setObjectName("clip");
			int idx = i;
			connect(clip, &QPushButton::clicked, clip, [idx] { sr_registry_save_index((size_t)idx); });
			r2->addWidget(clip);
			cl->addLayout(r2);

			cardsBox->addWidget(card);
			Card c{card, dot, stt, tm, clip, i};
			cards.push_back(c);
		}
	}

	void refresh()
	{
		struct sr_status_row rows[64];
		size_t n = sr_registry_snapshot(rows, 64);

		/* Signature: rebuild cards only when the camera set changes. */
		QString sig = QString::number(n);
		for (size_t i = 0; i < n; i++)
			sig += "|" + QString::fromUtf8(rows[i].name);
		if (sig != lastSig) {
			rebuildCards(rows, n);
			lastSig = sig;
		}

		int rec = 0, buf = 0, idle = 0, err = 0;
		for (int i = 0; i < (int)n && i < (int)cards.size(); i++) {
			Card &c = cards[i];
			QString st;
			QColor col;
			bool buffering = false;
			switch (rows[i].status) {
			case SR_STATUS_RECORDING:
				if (rows[i].use_buffer) {
					st = QStringLiteral("● BUF");
					col = QColor(IR_GOLD);
					buffering = true;
					buf++;
				} else {
					st = T("InstantRecord.Dock.Rec");
					col = QColor(IR_RED);
					rec++;
				}
				break;
			case SR_STATUS_ERROR:
				st = T("InstantRecord.Dock.Error");
				col = QColor("#f0a050");
				err++;
				break;
			default:
				st = T("InstantRecord.Dock.Idle");
				col = QColor(IR_MUTED);
				idle++;
				break;
			}
			c.dot->setStyleSheet(QString("color:%1;").arg(col.name()));
			c.status->setText(st);
			c.status->setStyleSheet(QString("font-weight:800;color:%1;").arg(col.name()));
			c.time->setText(format_elapsed(rows[i].elapsed_ns));
			bool showClip = rows[i].use_buffer && rows[i].status == SR_STATUS_RECORDING;
			c.clip->setVisible(showClip);
			(void)buffering;
		}
		counters->setText(QString("<span style='color:" IR_RED "'>%1 REC</span>  "
					  "<span style='color:" IR_GOLD "'>%2 BUF</span>  "
					  "<span style='color:" IR_MUTED "'>%3 idle</span>")
					  .arg(rec)
					  .arg(buf)
					  .arg(idle));
	}

	HotkeyButton *makeHkAll(const QString &label, int which)
	{
		auto *b = new HotkeyButton();
		b->setObjectName("hk");
		b->setText(label);
		connect(b, &QPushButton::clicked, b, [b] { b->startCapture(); });
		b->onCapture = [b, which, label](uint32_t mods, int key) {
			sr_registry_bind_hotkey_all(which, mods, key);
			b->setText(label + " \xE2\x9C\x93"); /* ✓ */
		};
		return b;
	}

	void pickAndAddCameras()
	{
		std::vector<QString> names;
		obs_enum_sources(sr_collect_sources, &names);

		QDialog dlg(this);
		dlg.setStyleSheet("QDialog{background:" IR_BG ";color:" IR_TEXT ";}"
				  "QLabel{color:" IR_TEXT ";}"
				  "QListWidget{background:" IR_CARD ";border:1px solid #17171b;border-radius:8px;}"
				  "QPushButton{background:#161618;color:" IR_TEXT
				  ";border:1px solid #2a2a2e;border-radius:14px;padding:7px 14px;}"
				  "QPushButton#apply{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd27a,stop:1 #e9a92f);color:#3a2a08;border:none;font-weight:800;}");
		dlg.setWindowTitle(T("InstantRecord.Add.Title"));
		auto *v = new QVBoxLayout(&dlg);

		QListWidget *list = nullptr;
		if (names.empty()) {
			v->addWidget(new QLabel(T("InstantRecord.Add.None")));
		} else {
			v->addWidget(new QLabel(T("InstantRecord.Add.Pick")));
			list = new QListWidget(&dlg);
			for (const QString &n : names) {
				auto *it = new QListWidgetItem(n, list);
				it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
				it->setCheckState(Qt::Checked);
			}
			v->addWidget(list);
		}

		auto *row = new QHBoxLayout();
		row->addStretch();
		auto *cancel = new QPushButton(T("InstantRecord.Global.Cancel"), &dlg);
		auto *ok = new QPushButton(T("InstantRecord.Add.Confirm"), &dlg);
		ok->setObjectName("apply");
		ok->setEnabled(list != nullptr);
		row->addWidget(cancel);
		row->addWidget(ok);
		v->addLayout(row);

		connect(cancel, &QPushButton::clicked, &dlg, [&] { dlg.reject(); });
		connect(ok, &QPushButton::clicked, &dlg, [&] {
			if (list) {
				for (int i = 0; i < list->count(); i++) {
					QListWidgetItem *it = list->item(i);
					if (it->checkState() == Qt::Checked)
						sr_add_filter_to_source(it->text());
				}
			}
			dlg.accept();
		});

		dlg.exec();
		refresh();
	}

	void openGlobalConfig()
	{
		QDialog dlg(this);
		dlg.setStyleSheet("QDialog{background:" IR_BG ";color:" IR_TEXT ";}"
				  "QLabel{color:" IR_TEXT ";}"
				  "QPushButton{background:#161618;color:" IR_TEXT
				  ";border:1px solid #2a2a2e;border-radius:14px;padding:7px 14px;}"
				  "QPushButton#apply{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd27a,stop:1 #e9a92f);color:#3a2a08;border:none;font-weight:800;}"
				  "QPushButton#hk{background:#141416;border:1px solid #26262b;border-radius:9px;padding:4px 10px;}");
		dlg.setWindowTitle(T("InstantRecord.Global.Title"));
		auto *grid = new QGridLayout(&dlg);
		int r = 0;

		grid->addWidget(new QLabel(T("InstantRecord.Global.OutputFolder")), r, 0);
		auto *pathEdit = new QLineEdit(&dlg);
		auto *browse = new QPushButton(T("InstantRecord.Global.Browse"), &dlg);
		connect(browse, &QPushButton::clicked, &dlg, [&] {
			QString d = QFileDialog::getExistingDirectory(&dlg);
			if (!d.isEmpty())
				pathEdit->setText(d);
		});
		grid->addWidget(pathEdit, r, 1);
		grid->addWidget(browse, r++, 2);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Container")), r, 0);
		auto *fmt = new QComboBox(&dlg);
		fmt->addItem("mkv", "mkv");
		fmt->addItem("mp4", "mp4");
		fmt->addItem("mov", "mov");
		grid->addWidget(fmt, r++, 1);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Encoder")), r, 0);
		auto *enc = new QComboBox(&dlg);
		const char *encId;
		size_t ei = 0;
		while (obs_enum_encoder_types(ei++, &encId)) {
			if (obs_get_encoder_type(encId) != OBS_ENCODER_VIDEO)
				continue;
			const char *nm = obs_encoder_get_display_name(encId);
			enc->addItem(nm ? nm : encId, QString(encId));
		}
		grid->addWidget(enc, r++, 1);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Bitrate")), r, 0);
		auto *bitrate = new QSpinBox(&dlg);
		bitrate->setRange(500, 100000);
		bitrate->setSingleStep(500);
		bitrate->setValue(6000);
		grid->addWidget(bitrate, r++, 1);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Trigger")), r, 0);
		auto *mode = new QComboBox(&dlg);
		mode->addItem(T("InstantRecord.Global.Trigger.Main"), 0);
		mode->addItem(T("InstantRecord.Global.Trigger.Manual"), 1);
		mode->addItem(T("InstantRecord.Global.Trigger.Always"), 2);
		grid->addWidget(mode, r++, 1);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Scale")), r, 0);
		auto *scale = new QComboBox(&dlg);
		scale->addItem(T("InstantRecord.Global.Scale.Native"), 0);
		scale->addItem("1080p", 1);
		scale->addItem("720p", 2);
		scale->addItem("480p", 3);
		grid->addWidget(scale, r++, 1);

		grid->addWidget(new QLabel(T("InstantRecord.Global.Fps")), r, 0);
		auto *fps = new QComboBox(&dlg);
		fps->addItem(T("InstantRecord.Global.Fps.Full"), 1);
		fps->addItem(T("InstantRecord.Global.Fps.Half"), 2);
		fps->addItem(T("InstantRecord.Global.Fps.Quarter"), 4);
		grid->addWidget(fps, r++, 1);

		auto *buffer = new QCheckBox(T("InstantRecord.Global.Buffer"), &dlg);
		grid->addWidget(buffer, r++, 1);
		grid->addWidget(new QLabel(T("InstantRecord.Global.BufferSecs")), r, 0);
		auto *bufSecs = new QSpinBox(&dlg);
		bufSecs->setRange(5, 600);
		bufSecs->setSingleStep(5);
		bufSecs->setValue(30);
		grid->addWidget(bufSecs, r++, 1);

		auto *isolate = new QCheckBox(T("InstantRecord.Global.IsolateAudio"), &dlg);
		grid->addWidget(isolate, r++, 1);

		/* Global hotkeys — bind the same key to every camera at once. */
		grid->addWidget(new QLabel(T("InstantRecord.Global.Hotkeys")), r, 0);
		auto *hkRow = new QHBoxLayout();
		hkRow->addWidget(makeHkAll(T("InstantRecord.Global.HkStart"), SR_HK_START));
		hkRow->addWidget(makeHkAll(T("InstantRecord.Global.HkStop"), SR_HK_STOP));
		hkRow->addWidget(makeHkAll(T("InstantRecord.Global.HkSave"), SR_HK_SAVE));
		hkRow->addStretch();
		grid->addLayout(hkRow, r++, 1, 1, 2);

		obs_data_t *saved = obs_data_create_from_json_file(sr_cfg_file().c_str());
		if (saved) {
			pathEdit->setText(QString::fromUtf8(obs_data_get_string(saved, "path")));
			selectByData(fmt, QString(obs_data_get_string(saved, "container")));
			selectByData(enc, QString(obs_data_get_string(saved, "encoder")));
			if (obs_data_get_int(saved, "bitrate") > 0)
				bitrate->setValue((int)obs_data_get_int(saved, "bitrate"));
			selectByData(mode, (int)obs_data_get_int(saved, "trigger"));
			selectByData(scale, (int)obs_data_get_int(saved, "scale"));
			selectByData(fps, (int)obs_data_get_int(saved, "fps"));
			buffer->setChecked(obs_data_get_bool(saved, "use_buffer"));
			if (obs_data_get_int(saved, "buffer_seconds") > 0)
				bufSecs->setValue((int)obs_data_get_int(saved, "buffer_seconds"));
			isolate->setChecked(obs_data_get_bool(saved, "isolate"));
			obs_data_release(saved);
		}

		auto *apply = new QPushButton(T("InstantRecord.Global.ApplyAll"), &dlg);
		apply->setObjectName("apply");
		auto *cancel = new QPushButton(T("InstantRecord.Global.Cancel"), &dlg);
		auto *bl = new QHBoxLayout();
		bl->addStretch();
		bl->addWidget(cancel);
		bl->addWidget(apply);
		grid->addLayout(bl, r, 0, 1, 3);

		connect(cancel, &QPushButton::clicked, &dlg, [&] { dlg.reject(); });
		connect(apply, &QPushButton::clicked, &dlg, [&] {
			QByteArray path = pathEdit->text().toUtf8();
			QByteArray fmtId = fmt->currentData().toString().toUtf8();
			QByteArray encStr = enc->currentData().toString().toUtf8();

			struct sr_global_config cfg;
			cfg.path = path.isEmpty() ? nullptr : path.constData();
			cfg.rec_format = fmtId.constData();
			cfg.encoder_id = encStr.isEmpty() ? nullptr : encStr.constData();
			cfg.bitrate = bitrate->value();
			cfg.record_mode = mode->currentData().toInt();
			cfg.isolate_audio = isolate->isChecked() ? 1 : 0;
			cfg.scale_mode = scale->currentData().toInt();
			cfg.fps_divisor = fps->currentData().toInt();
			cfg.use_buffer = buffer->isChecked() ? 1 : 0;
			cfg.buffer_seconds = bufSecs->value();
			sr_registry_apply_config(&cfg);

			obs_data_t *d = obs_data_create();
			obs_data_set_string(d, "path", path.constData());
			obs_data_set_string(d, "container", fmtId.constData());
			obs_data_set_string(d, "encoder", encStr.constData());
			obs_data_set_int(d, "bitrate", bitrate->value());
			obs_data_set_int(d, "trigger", mode->currentData().toInt());
			obs_data_set_int(d, "scale", scale->currentData().toInt());
			obs_data_set_int(d, "fps", fps->currentData().toInt());
			obs_data_set_bool(d, "use_buffer", buffer->isChecked());
			obs_data_set_int(d, "buffer_seconds", bufSecs->value());
			obs_data_set_bool(d, "isolate", isolate->isChecked());
			obs_data_save_json_safe(d, sr_cfg_file().c_str(), "tmp", "bak");
			obs_data_release(d);

			dlg.accept();
		});

		dlg.exec();
		refresh();
	}
};

void instant_record_register_dock(void)
{
	auto *dock = new InstantRecordDock();
	obs_frontend_add_dock_by_id("instant_record_dock", "Instant Record", dock);
}

#else /* built without frontend/Qt */
void instant_record_register_dock(void) {}
#endif
