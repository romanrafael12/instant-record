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
#include "clean-program.h"

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>

#include <string>
#include <vector>
#include <cstring>
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
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QMap>
#include <QVariant>
#include <QGraphicsDropShadowEffect>
#include <QScrollArea>
#include <QLayout>
#include <QStyle>
#include <QList>
#include <QRect>
#include <QMargins>
#include <QProgressBar>
#include <QResizeEvent>
#include <QLocale>
#include <QMenu>
#include <QAction>

/* Instant Replay brand palette (from the app UI). */
#define IR_BG "#0b0b0d"
#define IR_CARD "#141416"
#define IR_RED "#ef4a44"
#define IR_GOLD "#f7c04a"
#define IR_BLUE "#3a8fe0"
#define IR_PURPLE "#7b5cf0"
#define IR_GREEN "#4ade80"
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

/* Only real video feeds — cameras, browsers, media, captures, images.
 * Excludes text/color overlays, filters, transitions, audio. */
static bool sr_is_recordable(obs_source_t *src)
{
	uint32_t flags = obs_source_get_output_flags(src);
	if (!(flags & OBS_SOURCE_VIDEO))
		return false;
	enum obs_source_type t = obs_source_get_type(src);
	if (t == OBS_SOURCE_TYPE_FILTER || t == OBS_SOURCE_TYPE_TRANSITION)
		return false;
	const char *id = obs_source_get_id(src);
	if (!id)
		return false;
	if (strncmp(id, "text_", 5) == 0)
		return false; /* text (GDI+/FreeType) can't carry a real feed */
	if (strncmp(id, "color_source", 12) == 0)
		return false; /* solid color */
	return true;
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

/* Collect names of recordable video sources that don't have the filter. */
static bool sr_collect_sources(void *data, obs_source_t *src)
{
	auto *names = static_cast<std::vector<QString> *>(data);
	if (!sr_is_recordable(src) || sr_source_has_filter(src))
		return true;
	const char *nm = obs_source_get_name(src);
	if (nm && *nm)
		names->push_back(QString::fromUtf8(nm));
	return true;
}

/* Add the filter to a source by name. Returns true on success. */
static bool sr_add_filter_to_source(const QString &name)
{
	if (name.isEmpty())
		return false;
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (!src)
		return false;
	bool ok = sr_is_recordable(src) && !sr_source_has_filter(src);
	if (ok) {
		obs_source_t *flt = obs_source_create_private("instant_record_filter", "Instant Record", nullptr);
		if (flt) {
			obs_source_filter_add(src, flt);
			obs_source_release(flt);
		}
	}
	obs_source_release(src);
	return ok;
}

/* Remove the Instant Record filter from a source by name. */
static void sr_remove_filter_from_source(const QString &name)
{
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (!src)
		return;
	obs_source_t *flt = obs_source_get_filter_by_name(src, "Instant Record");
	if (flt) {
		obs_source_filter_remove(src, flt);
		obs_source_release(flt);
	}
	obs_source_release(src);
}

/* ---- Auto encoder selection ---------------------------------------- */

static QString sr_encoder_family(const QString &id)
{
	if (id.contains("nvenc"))
		return "nvenc";
	if (id.contains("amf") || id.contains("_amd"))
		return "amf";
	if (id.contains("qsv"))
		return "qsv";
	if (id.contains("videotoolbox"))
		return "videotoolbox";
	if (id.contains("x264"))
		return "x264";
	return "other";
}

/* Keep the best id for a family, preferring an H.264 variant (edits
 * cleanly everywhere) over HEVC/AV1. */
static void sr_consider_enc(QString &slot, const QString &id)
{
	bool idH264 = id.contains("h264") || id.contains("x264") || id.contains("avc");
	bool slotH264 = slot.contains("h264") || slot.contains("x264") || slot.contains("avc");
	/* VideoToolbox: the "ave.avc" id is the Apple hardware H.264 encoder. */
	bool idAve = id.contains("ave.avc");
	bool slotAve = slot.contains("ave.avc");
	if (slot.isEmpty() || (idAve && !slotAve) || (!slotAve && idH264 && !slotH264))
		slot = id;
}

/* Family the live stream is currently encoding with (empty if not
 * streaming). Used to send ISO recording to a *different* encoder. */
static QString sr_stream_family()
{
	QString fam;
	obs_output_t *out = obs_frontend_get_streaming_output();
	if (out) {
		obs_encoder_t *venc = obs_output_get_video_encoder(out);
		if (venc)
			fam = sr_encoder_family(QString::fromUtf8(obs_encoder_get_id(venc)));
		obs_output_release(out);
	}
	return fam;
}

/* Pick the best available encoder: hardware first (NVENC > AMF > QSV >
 * VideoToolbox > x264), and prefer a hardware encoder that isn't the same
 * family the stream is using so the ISO recording doesn't fight the stream
 * for the same chip. If there is no spare hardware, share the stream's
 * hardware encoder rather than falling back to CPU x264.
 * Adapts to whatever hardware the machine actually has. */
static QString sr_resolve_auto_encoder()
{
	QString nv, amf, qsv, vt, x264;
	const char *id;
	size_t i = 0;
	while (obs_enum_encoder_types(i++, &id)) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		QString s = QString::fromUtf8(id);
		QString fam = sr_encoder_family(s);
		if (fam == "nvenc")
			sr_consider_enc(nv, s);
		else if (fam == "amf")
			sr_consider_enc(amf, s);
		else if (fam == "qsv")
			sr_consider_enc(qsv, s);
		else if (fam == "videotoolbox")
			sr_consider_enc(vt, s);
		else if (fam == "x264")
			sr_consider_enc(x264, s);
	}
	const QString fams[5] = {"nvenc", "amf", "qsv", "videotoolbox", "x264"};
	QString ids[5] = {nv, amf, qsv, vt, x264};
	QString streamFam = sr_stream_family();
	/* Best HARDWARE encoder whose family differs from the stream's. */
	for (int k = 0; k < 4; k++)
		if (!ids[k].isEmpty() && fams[k] != streamFam)
			return ids[k];
	/* Otherwise the best available, even if it shares with the stream. */
	for (int k = 0; k < 5; k++)
		if (!ids[k].isEmpty())
			return ids[k];
	return "obs_x264";
}

/* Wraps child widgets to as many columns as fit the current width, so
 * the camera grid adapts to how wide the dock is. Based on the classic
 * Qt FlowLayout example (no Q_OBJECT needed). */
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int spacing = 8) : QLayout(parent), m_space(spacing)
	{
		setContentsMargins(0, 0, 0, 0);
	}
	~FlowLayout() override
	{
		QLayoutItem *item;
		while ((item = takeAt(0)))
			delete item;
	}
	void addItem(QLayoutItem *item) override { itemList.append(item); }
	int count() const override { return (int)itemList.size(); }
	QLayoutItem *itemAt(int i) const override { return itemList.value(i); }
	QLayoutItem *takeAt(int i) override
	{
		return (i >= 0 && i < itemList.size()) ? itemList.takeAt(i) : nullptr;
	}
	Qt::Orientations expandingDirections() const override { return {}; }
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
	void setGeometry(const QRect &r) override
	{
		QLayout::setGeometry(r);
		doLayout(r, false);
	}
	QSize sizeHint() const override { return minimumSize(); }
	QSize minimumSize() const override
	{
		QSize s;
		for (QLayoutItem *item : itemList)
			s = s.expandedTo(item->minimumSize());
		return s;
	}

private:
	int doLayout(const QRect &rect, bool testOnly) const
	{
		int x = rect.x(), y = rect.y(), lineHeight = 0;
		for (QLayoutItem *item : itemList) {
			int nextX = x + item->sizeHint().width() + m_space;
			if (nextX - m_space > rect.right() && lineHeight > 0) {
				x = rect.x();
				y = y + lineHeight + m_space;
				nextX = x + item->sizeHint().width() + m_space;
				lineHeight = 0;
			}
			if (!testOnly)
				item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
			x = nextX;
			lineHeight = qMax(lineHeight, item->sizeHint().height());
		}
		return y + lineHeight - rect.y();
	}
	QList<QLayoutItem *> itemList;
	int m_space;
};

struct Card {
	QFrame *w;
	QLabel *dot;
	QLabel *info;
	QLabel *status;
	QLabel *time;
	QLabel *pct;          /* health pill: % of frames skipped */
	QLabel *skipped;      /* "812 skipped / of 45,210" (large layout only) */
	QProgressBar *lagBar; /* lag bar (large layout only) */
	QPushButton *clip;
	QPushButton *del;
	QString name;
	int index;
};

/* The dock picks one of three layouts from its own width, so it can be
 * dragged as narrow as you like without the bottom buttons colliding. */
enum DockLayout { LAYOUT_SMALL = 0, LAYOUT_MEDIUM = 1, LAYOUT_LARGE = 2 };
#define IR_LAYOUT_MEDIUM_MIN 360
#define IR_LAYOUT_LARGE_MIN 520

static int layout_for_width(int w)
{
	if (w < IR_LAYOUT_MEDIUM_MIN)
		return LAYOUT_SMALL;
	if (w < IR_LAYOUT_LARGE_MIN)
		return LAYOUT_MEDIUM;
	return LAYOUT_LARGE;
}

/* What the compact bottom bar's mode button cycles through. */
enum DockMode { MODE_BUFFER = 0, MODE_RECORD = 1, MODE_CLEAN = 2 };

static const char *mode_key(int m)
{
	return m == MODE_RECORD ? "record" : (m == MODE_CLEAN ? "clean" : "buffer");
}

static int mode_from_key(const char *k)
{
	if (k && strcmp(k, "record") == 0)
		return MODE_RECORD;
	if (k && strcmp(k, "clean") == 0)
		return MODE_CLEAN;
	return MODE_BUFFER;
}

static const char *mode_text_key(int m, bool small)
{
	if (m == MODE_RECORD)
		return small ? "InstantRecord.Dock.ModeShort.Record" : "InstantRecord.Dock.Mode.Record";
	if (m == MODE_CLEAN)
		return small ? "InstantRecord.Dock.ModeShort.Clean" : "InstantRecord.Dock.Mode.Clean";
	return "InstantRecord.Dock.Mode.Buffer";
}

/* ---- per-camera health ---------------------------------------------- */

struct Health {
	double pct;
	int level; /* 0 ok, 1 amber, 2 red */
};

static Health row_health(const struct sr_status_row &r)
{
	Health h = {0.0, 0};
	if (r.status == SR_STATUS_ERROR) {
		h.level = 2;
		return h;
	}
	/* Don't judge until there are enough frames to mean something. */
	if (r.status != SR_STATUS_RECORDING || r.total_frames < 120)
		return h;
	h.pct = 100.0 * (double)r.skipped_frames / (double)r.total_frames;
	h.level = h.pct >= 2.0 ? 2 : (h.pct >= 0.5 ? 1 : 0);
	return h;
}

static QString level_color(int level)
{
	return QString::fromLatin1(level == 2 ? IR_RED : (level == 1 ? IR_GOLD : IR_GREEN));
}

static QString level_bg(int level)
{
	return QString::fromLatin1(level == 2 ? "#33141a" : (level == 1 ? "#2a2210" : "#12301d"));
}

static QString num(qulonglong v)
{
	return QLocale().toString(v);
}

/* Short, readable name for an encoder id. */
static QString enc_label(const char *id)
{
	if (!id || !*id)
		return QString();
	QString fam = sr_encoder_family(QString::fromUtf8(id));
	if (fam == "nvenc")
		return QStringLiteral("NVENC");
	if (fam == "amf")
		return QStringLiteral("AMF");
	if (fam == "qsv")
		return QStringLiteral("QSV");
	if (fam == "videotoolbox")
		return QStringLiteral("VideoToolbox");
	if (fam == "x264")
		return QStringLiteral("x264");
	return QString::fromUtf8(id);
}

/* Folder to report free disk space for: the configured output folder,
 * else OBS's recording folder. */
static std::string resolve_out_path()
{
	std::string p;
	obs_data_t *saved = obs_data_create_from_json_file(sr_cfg_file().c_str());
	if (saved) {
		const char *s = obs_data_get_string(saved, "path");
		if (s && *s)
			p = s;
		obs_data_release(saved);
	}
	if (p.empty()) {
		char *rec = obs_frontend_get_current_record_output_path();
		if (rec) {
			p = rec;
			bfree(rec);
		}
	}
	return p;
}

static QString disk_free_text(const std::string &path)
{
	uint64_t bytes = os_get_free_disk_space(path.empty() ? "." : path.c_str());
	return QString::asprintf("%.0f GB", (double)bytes / 1e9);
}

class InstantRecordDock : public QWidget {
public:
	explicit InstantRecordDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		setStyleSheet(
			"QWidget{background:" IR_BG ";color:" IR_TEXT ";font-size:12px;}"
			"QLabel{background:transparent;}"
			"QLabel#title{font-size:14px;font-weight:800;}"
			"QLabel#subtitle{font-size:10px;font-weight:600;color:" IR_GOLD ";}"
			"QLabel#drop{border:2px dashed #24242a;border-radius:10px;color:#5a5f65;padding:12px;}"
			"QFrame#card{background:" IR_CARD ";border:1px solid #242428;border-radius:12px;}"
			"QListWidget{background:" IR_CARD ";border:1px solid #242428;border-radius:8px;}"
			"QPushButton{background:#1a1a1e;color:" IR_TEXT
			";border:1px solid #2e2e34;border-radius:12px;padding:5px 11px;font-weight:700;font-size:12px;}"
			"QPushButton:hover{border-color:" IR_GOLD ";}"
			"QPushButton#start{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ef4a44,stop:1 #cc302b);border:none;color:#fff;}"
			"QPushButton#save{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f7c04a,stop:1 #e9a41f);border:none;color:#3a2a08;}"
			"QPushButton#add{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #7b5cf0,stop:1 #5f3fd0);border:none;color:#fff;}"
			"QPushButton#addtop{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #7b5cf0,stop:1 #5f3fd0);border:none;color:#fff;border-radius:14px;font-size:16px;font-weight:800;padding:0;}"
			"QPushButton#addtop:hover{background:#8b6cff;}"
			"QPushButton#updates{background:transparent;border:none;color:#7c848c;font-size:11px;font-weight:600;padding:2px 4px;text-decoration:underline;}"
			"QPushButton#updates:hover{color:" IR_GOLD ";}"
			"QPushButton#apply{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f7c04a,stop:1 #e9a41f);border:none;color:#3a2a08;font-weight:800;}"
			"QPushButton#clip{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #3a8fe0,stop:1 #2472c8);border:none;color:#fff;border-radius:12px;padding:4px 14px;}"
			"QPushButton#del{background:transparent;border:none;color:#5a5f65;font-weight:800;padding:2px 6px;}"
			"QPushButton#del:hover{color:" IR_RED ";}"
			"QScrollArea#cardscroll{background:transparent;border:none;}"
			"QWidget#scrollcontent{background:transparent;}"
			"QPushButton#sizebtn{background:#1a1a1e;color:#9aa0a6;border:1px solid #2e2e34;border-radius:8px;padding:4px 0;font-weight:800;}"
			"QPushButton#sizebtn:checked{background:" IR_PURPLE ";color:#fff;border:none;}"
			"QPushButton#link{background:transparent;border:none;color:" IR_BLUE ";font-weight:700;padding:2px 6px;}"
			"QPushButton#mode{background:#1d1733;border:1px solid #4a3a92;color:#c9bcff;border-radius:12px;padding:5px 11px;font-weight:700;}"
			"QPushButton#mode:hover{border-color:" IR_PURPLE ";}"
			"QPushButton#mode:disabled{background:#141416;border:1px solid #2e2e34;color:#7c848c;}"
			"QMenu{background:#141416;border:1px solid #4a3a92;border-radius:10px;padding:4px;}"
			"QMenu::item{padding:6px 16px;border-radius:8px;}"
			"QMenu::item:selected{background:#1d1733;color:#c9bcff;}"
			"QFrame#stat{background:" IR_CARD ";border:1px solid #242428;border-radius:10px;}"
			"QWidget#tsbox{background:transparent;}");

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
			logo->setPixmap(pix.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		header->addWidget(logo);
		auto *titleBox = new QVBoxLayout();
		titleBox->setSpacing(0);
		auto *title = new QLabel("Instant Record", this);
		title->setObjectName("title");
		subtitle = new QLabel("by Instant Replay", this);
		subtitle->setObjectName("subtitle");
		titleBox->addWidget(title);
		titleBox->addWidget(subtitle);
		header->addLayout(titleBox);
		header->addStretch();
		alertPill = new QLabel(this);
		alertPill->setVisible(false);
		header->addWidget(alertPill);
		counters = new QLabel(this);
		counters->setStyleSheet("font-size:11px;font-weight:700;");
		header->addWidget(counters);
		auto *addTop = new QPushButton(QString::fromUtf8("\xEF\xBC\x8B"), this); /* ＋ */
		addTop->setObjectName("addtop");
		addTop->setToolTip(T("InstantRecord.Dock.AddCams"));
		addTop->setFixedSize(28, 28);
		header->addSpacing(8);
		header->addWidget(addTop);
		root->addLayout(header);

		/* Health alert banner: only shown while a camera is skipping frames. */
		alertBanner = new QFrame(this);
		alertBanner->setObjectName("alert");
		{
			auto *al = new QHBoxLayout(alertBanner);
			al->setContentsMargins(10, 6, 8, 6);
			al->setSpacing(8);
			alertIcon = new QLabel(QString::fromUtf8("\xE2\x9A\xA0"), alertBanner); /* warning sign */
			alertText = new QLabel(alertBanner);
			alertText->setWordWrap(true);
			alertReset = new QPushButton(T("InstantRecord.Health.Reset"), alertBanner);
			alertReset->setToolTip(T("InstantRecord.Health.ResetTip"));
			al->addWidget(alertIcon);
			al->addWidget(alertText, 1);
			al->addWidget(alertReset);
			connect(alertReset, &QPushButton::clicked, this, [this] { resetLag(); });
		}
		alertBanner->setVisible(false);
		root->addWidget(alertBanner);

		/* Stats strip (large layout): the same numbers OBS Stats shows, but
		 * for the Instant Record cameras. */
		statsStrip = new QWidget(this);
		{
			auto *sl = new QHBoxLayout(statsStrip);
			sl->setContentsMargins(0, 0, 0, 0);
			sl->setSpacing(8);
			auto makeStat = [&](int idx, const char *titleKey, QLabel **valueOut) {
				auto *f = new QFrame(statsStrip);
				f->setObjectName("stat");
				auto *v = new QVBoxLayout(f);
				v->setContentsMargins(10, 6, 10, 6);
				v->setSpacing(0);
				auto *tl = new QLabel(T(titleKey), f);
				tl->setStyleSheet("color:" IR_MUTED ";font-size:11px;");
				auto *val = new QLabel(QStringLiteral("--"), f);
				val->setTextFormat(Qt::RichText);
				v->addWidget(tl);
				v->addWidget(val);
				sl->addWidget(f, 1);
				statFrame[idx] = f;
				statTitle[idx] = tl;
				*valueOut = val;
			};
			makeStat(0, "InstantRecord.Health.EncoderLag", &statEnc);
			makeStat(1, "InstantRecord.Health.RenderLag", &statRender);
			makeStat(2, "InstantRecord.Health.DiskFree", &statDisk);
			/* Small layout: the three numbers collapse into one line. */
			statLine = new QLabel(statsStrip);
			statLine->setTextFormat(Qt::RichText);
			statLine->setStyleSheet("background:" IR_CARD ";border:1px solid #242428;border-radius:10px;"
						"padding:5px 9px;font-size:11px;");
			statLine->setVisible(false);
			sl->addWidget(statLine, 1);
			statReset = new QPushButton(T("InstantRecord.Health.Reset"), statsStrip);
			statReset->setToolTip(T("InstantRecord.Health.ResetTip"));
			sl->addWidget(statReset);
			connect(statReset, &QPushButton::clicked, this, [this] { resetLag(); });
		}
		statsStrip->setVisible(false);
		root->addWidget(statsStrip);

		/* Clean Program live card — hidden until a clean-program
		 * recording is running. Shows elapsed time, the camera being
		 * followed right now, and a Stop button. */
		cleanCard = new QFrame(this);
		cleanCard->setObjectName("cleancard");
		cleanCard->setStyleSheet("QFrame#cleancard{background:#101a26;border:1px solid " IR_BLUE
					 ";border-radius:12px;}");
		{
			auto *cc = new QVBoxLayout(cleanCard);
			cc->setContentsMargins(10, 6, 10, 7);
			cc->setSpacing(2);
			auto *top = new QHBoxLayout();
			top->setSpacing(8);
			auto *recBadge = new QLabel(QString::fromUtf8("\xE2\x97\x8F REC"), cleanCard);
			recBadge->setStyleSheet("background:" IR_RED ";color:#fff;font-weight:800;"
						"font-size:10px;border-radius:8px;padding:2px 8px;");
			cleanTtl = new QLabel(T("InstantRecord.Clean.Title"), cleanCard);
			auto *ttl = cleanTtl;
			ttl->setStyleSheet("color:" IR_TEXT ";font-weight:700;font-size:12px;");
			cleanTime = new QLabel(QStringLiteral("00:00:00"), cleanCard);
			cleanTime->setStyleSheet("color:" IR_BLUE ";font-weight:800;"
						 "font-family:'Consolas','Menlo',monospace;");
			top->addWidget(recBadge);
			top->addWidget(ttl);
			top->addStretch();
			top->addWidget(cleanTime);
			cc->addLayout(top);
			cleanCam = new QLabel(cleanCard);
			cleanCam->setStyleSheet("font-size:11px;");
			cc->addWidget(cleanCam);
		}
		cleanCard->setVisible(false);
		root->addWidget(cleanCard);

		/* Scrollable, width-adaptive grid of camera cards. Lives in a
		 * scroll area so many cameras never push the header or the
		 * bottom bar off-screen. */
		auto *scroll = new QScrollArea(this);
		scroll->setObjectName("cardscroll");
		scroll->setWidgetResizable(true);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll->setFrameShape(QFrame::NoFrame);
		auto *scrollContent = new QWidget();
		scrollContent->setObjectName("scrollcontent");
		cardsBox = new QVBoxLayout(scrollContent);
		cardsBox->setContentsMargins(0, 0, 0, 0);
		cardsBox->setSpacing(6);
		scroll->setWidget(scrollContent);
		root->addWidget(scroll, 1); /* stretch: takes the free space */

		/* Empty-state hint. */
		emptyHint = new QLabel(T("InstantRecord.Dock.EmptyHint"), this);
		emptyHint->setObjectName("drop");
		emptyHint->setAlignment(Qt::AlignCenter);
		emptyHint->setWordWrap(true);
		root->addWidget(emptyHint);
		setAcceptDrops(true); /* drag a source from OBS onto the dock */

		/* Bottom action bar. */
		fullBar = new QWidget(this);
		auto *btns = new QHBoxLayout(fullBar);
		btns->setContentsMargins(0, 0, 0, 0);
		startAll = new QPushButton(T("InstantRecord.Dock.StartAll"), this);
		startAll->setObjectName("start");
		auto *stopAll = new QPushButton(T("InstantRecord.Dock.StopAll"), this);
		auto *saveAll = new QPushButton(T("InstantRecord.Dock.SaveBuffer"), this);
		saveAll->setObjectName("save");
		auto *config = new QPushButton(T("InstantRecord.Dock.Config"), this);
		cleanBtn = new QPushButton(T("InstantRecord.Clean.Start"), this);
		cleanBtn->setObjectName("cleanbtn");
		btns->addWidget(startAll);
		btns->addWidget(stopAll);
		btns->addWidget(saveAll);
		btns->addWidget(cleanBtn);
		btns->addStretch();
		btns->addWidget(config);
		root->addWidget(fullBar);

		/* Compact bar (small / medium layouts): one mode button + one action
		 * button, so the dock can be dragged much narrower. */
		compactBar = new QWidget(this);
		{
			auto *cb = new QHBoxLayout(compactBar);
			cb->setContentsMargins(0, 0, 0, 0);
			cb->setSpacing(6);
			modeBtn = new QPushButton(compactBar);
			modeBtn->setObjectName("mode");
			modeBtn->setToolTip(T("InstantRecord.Dock.ModeTip"));
			actBtn = new QPushButton(compactBar);
			actBtn->setObjectName("start");
			saveBtn = new QPushButton(compactBar);
			saveBtn->setObjectName("save");
			saveBtn->setToolTip(T("InstantRecord.Dock.SaveBuffer"));
			cfgBtn = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"), compactBar); /* gear */
			cfgBtn->setToolTip(T("InstantRecord.Dock.Config"));
			cb->addWidget(modeBtn);
			cb->addWidget(actBtn);
			cb->addWidget(saveBtn);
			cb->addStretch();
			cb->addWidget(cfgBtn);
			connect(modeBtn, &QPushButton::clicked, this, [this] { showModeMenu(); });
			connect(actBtn, &QPushButton::clicked, this, [this] { toggleRun(); });
			connect(saveBtn, &QPushButton::clicked, this, [] { sr_registry_save_all(); });
			connect(cfgBtn, &QPushButton::clicked, this, [this] { openGlobalConfig(); });
		}
		compactBar->setVisible(false);
		root->addWidget(compactBar);

		connect(cleanBtn, &QPushButton::clicked, this, [] { clean_program_toggle(); });

		connect(addTop, &QPushButton::clicked, this, [this] { pickAndAddCameras(); });
		connect(startAll, &QPushButton::clicked, this, [] { sr_registry_start_all(); });
		connect(stopAll, &QPushButton::clicked, this, [] { sr_registry_stop_all(); });
		connect(saveAll, &QPushButton::clicked, this, [] { sr_registry_save_all(); });
		connect(config, &QPushButton::clicked, this, [this] { openGlobalConfig(); });

		outPath = resolve_out_path();

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(500);
		refresh();

		/* Apply the saved sidecar preference at startup. */
		{
			obs_data_t *saved = obs_data_create_from_json_file(sr_cfg_file().c_str());
			if (saved) {
				if (obs_data_has_user_value(saved, "sidecar"))
					sr_set_sidecar_enabled(obs_data_get_bool(saved, "sidecar") ? 1 : 0);
				curMode = mode_from_key(obs_data_get_string(saved, "mode"));
				obs_data_release(saved);
			}
		}
	}

private:
	QVBoxLayout *cardsBox;
	QLabel *counters;
	QLabel *emptyHint;
	QPushButton *startAll;
	QFrame *cleanCard;
	QLabel *cleanTime;
	QLabel *cleanCam;
	QLabel *cleanTtl = nullptr;
	QPushButton *cleanBtn;
	std::vector<Card> cards;
	QString lastSig;
	bool blinkOn = false;
	QLabel *subtitle = nullptr;
	QLabel *alertPill = nullptr;
	QFrame *alertBanner = nullptr;
	QLabel *alertIcon = nullptr;
	QLabel *alertText = nullptr;
	QPushButton *alertReset = nullptr;
	QWidget *statsStrip = nullptr;
	QLabel *statEnc = nullptr;
	QLabel *statRender = nullptr;
	QLabel *statDisk = nullptr;
	QFrame *statFrame[3] = {nullptr, nullptr, nullptr};
	QLabel *statTitle[3] = {nullptr, nullptr, nullptr};
	QLabel *statLine = nullptr;
	QPushButton *statReset = nullptr;
	QWidget *fullBar = nullptr;
	QWidget *compactBar = nullptr;
	QPushButton *modeBtn = nullptr;
	QPushButton *actBtn = nullptr;
	QPushButton *saveBtn = nullptr;
	QPushButton *cfgBtn = nullptr;
	int layoutMode = LAYOUT_MEDIUM;
	int curMode = MODE_BUFFER;
	int liveCams = 0;
	uint32_t renderBaseLag = 0;
	uint32_t renderBaseTotal = 0;
	std::string outPath;

	void resetLag()
	{
		sr_registry_reset_lag();
		renderBaseLag = obs_get_lagged_frames();
		renderBaseTotal = obs_get_total_frames();
		refresh();
	}

	void applyModeToCameras()
	{
		if (curMode == MODE_CLEAN)
			return; /* clean program doesn't use the per-camera filters */
		struct sr_global_config cfg;
		cfg.path = nullptr;
		cfg.rec_format = nullptr;
		cfg.encoder_id = nullptr;
		cfg.bitrate = 0;
		cfg.record_mode = -1;
		cfg.isolate_audio = -1;
		cfg.scale_mode = -1;
		cfg.fps_divisor = 0;
		cfg.use_buffer = (curMode == MODE_BUFFER) ? 1 : 0;
		cfg.also_buffer = -1;
		cfg.buffer_seconds = 0;
		sr_registry_apply_config(&cfg);
	}

	void saveModeToConfig()
	{
		obs_data_t *d = obs_data_create_from_json_file(sr_cfg_file().c_str());
		if (!d)
			d = obs_data_create();
		obs_data_set_string(d, "mode", mode_key(curMode));
		if (curMode != MODE_CLEAN)
			obs_data_set_bool(d, "use_buffer", curMode == MODE_BUFFER);
		obs_data_save_json_safe(d, sr_cfg_file().c_str(), "tmp", "bak");
		obs_data_release(d);
	}

	/* Mode picker: a small menu under the mode button (no cycling). The
	 * button is disabled while anything is running, so the mode is never
	 * changed under a live recording. */
	void showModeMenu()
	{
		QMenu menu(this);
		const char *keys[3] = {"InstantRecord.Dock.Mode.Buffer", "InstantRecord.Dock.Mode.Record",
				       "InstantRecord.Dock.Mode.Clean"};
		for (int m = 0; m < 3; m++) {
			QString label = (m == curMode ? QString::fromUtf8("\xE2\x9C\x93  ") : QStringLiteral("     ")) + T(keys[m]);
			QAction *act = menu.addAction(label);
			connect(act, &QAction::triggered, this, [this, m] { setMode(m); });
		}
		/* The mode button sits at the bottom of the dock, so open the menu
		 * upward (above the button) instead of below it. */
		menu.ensurePolished();
		QPoint pos = modeBtn->mapToGlobal(QPoint(0, 0));
		pos.setY(pos.y() - menu.sizeHint().height() - 4);
		menu.exec(pos);
	}

	void setMode(int m)
	{
		if (m == curMode)
			return;
		if (clean_program_active())
			clean_program_stop();
		sr_registry_stop_all();
		curMode = m;
		applyModeToCameras();
		saveModeToConfig();
		refresh();
	}

	void toggleRun()
	{
		if (curMode == MODE_CLEAN)
			clean_program_toggle();
		else if (liveCams > 0)
			sr_registry_stop_all();
		else
			sr_registry_start_all();
		refresh();
	}

	void rebuildCards(struct sr_status_row *rows, size_t n)
	{
		for (auto &c : cards)
			c.w->deleteLater();
		cards.clear();
		QLayoutItem *item;
		while ((item = cardsBox->takeAt(0)) != nullptr) {
			if (item->widget())
				item->widget()->deleteLater();
			delete item;
		}

		const bool small = layoutMode == LAYOUT_SMALL;
		const bool large = layoutMode == LAYOUT_LARGE;

		for (int i = 0; i < (int)n; i++) {
			auto *card = new QFrame();
			card->setObjectName("card");
			auto *cl = new QHBoxLayout(card);
			cl->setContentsMargins(12, 8, 10, 8);
			cl->setSpacing(8);

			auto *dot = new QLabel(QString::fromUtf8("\xE2\x97\x8F"), card);
			dot->setStyleSheet("font-size:14px;");
			auto *nm = new QLabel(QString::fromUtf8(rows[i].name), card);
			nm->setStyleSheet("font-weight:600;");
			if (large)
				nm->setMinimumWidth(150);

			auto *pct = new QLabel(card);
			pct->setAlignment(Qt::AlignCenter);
			pct->setVisible(false); /* shown once the camera is live */

			auto *lagBar = new QProgressBar(card);
			lagBar->setRange(0, 100);
			lagBar->setTextVisible(false);
			lagBar->setFixedHeight(6);
			lagBar->setMinimumWidth(40);
			auto *skipped = new QLabel(card);
			skipped->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
			skipped->setMinimumWidth(78);
			if (!large) {
				lagBar->setVisible(false);
				skipped->setVisible(false);
			}

			auto *tsBox = new QWidget(card);
			tsBox->setObjectName("tsbox");
			auto *tsCol = new QVBoxLayout(tsBox);
			tsCol->setSpacing(1);
			tsCol->setContentsMargins(0, 0, 0, 0);
			auto *stt = new QLabel(tsBox);
			stt->setStyleSheet("font-weight:800;");
			stt->setAlignment(Qt::AlignRight);
			auto *tm = new QLabel(tsBox);
			tm->setStyleSheet("color:#c9d0d6;");
			tm->setAlignment(Qt::AlignRight);
			/* Time on top, BUF/REC status right below it. */
			tsCol->addWidget(tm);
			tsCol->addWidget(stt);
			if (small)
				tsBox->setVisible(false);

			auto *clip = new QPushButton(small ? QString::fromUtf8("\xE2\x98\x85") : T("InstantRecord.Dock.Save"), card);
			clip->setObjectName("clip");
			clip->setToolTip(T("InstantRecord.Dock.SaveBuffer"));
			int idx = i;
			connect(clip, &QPushButton::clicked, clip, [idx] { sr_registry_save_index((size_t)idx); });

			auto *del = new QPushButton(QString::fromUtf8("\xE2\x9C\x95"), card); /* ✕ */
			del->setObjectName("del");
			del->setToolTip(T("InstantRecord.Dock.Remove"));
			QString srcName = QString::fromUtf8(rows[i].name);
			connect(del, &QPushButton::clicked, del, [this, srcName] {
				sr_remove_filter_from_source(srcName);
				lastSig.clear(); /* force rebuild */
				refresh();
			});

			cl->addWidget(dot);
			cl->addWidget(nm);
			if (large)
				cl->addWidget(lagBar, 1);
			else
				cl->addStretch();
			cl->addWidget(pct);
			if (large)
				cl->addWidget(skipped);
			cl->addWidget(tsBox);
			cl->addSpacing(small ? 0 : 8);
			cl->addWidget(clip);
			cl->addWidget(del);

			cardsBox->addWidget(card);
			cards.push_back({card, dot, nm, stt, tm, pct, skipped, lagBar, clip, del,
							 QString::fromUtf8(rows[i].name), i});
		}
		cardsBox->addStretch(); /* keep cards packed at the top */
		emptyHint->setText(n == 0 ? T("InstantRecord.Dock.EmptyHint") : T("InstantRecord.Dock.DropHintShort"));
		emptyHint->setVisible(true);
	}

	void refresh()
	{
		blinkOn = !blinkOn;
		struct sr_status_row rows[64];
		size_t n = sr_registry_snapshot(rows, 64);

		QString sig = QString::number(n);
		for (size_t i = 0; i < n; i++)
			sig += "|" + QString::fromUtf8(rows[i].name);
		if (sig != lastSig) {
			rebuildCards(rows, n);
			lastSig = sig;
		}

		const int lm = layoutMode;
		const bool small = lm == LAYOUT_SMALL;
		const bool large = lm == LAYOUT_LARGE;
		int rec = 0, buf = 0, idle = 0, err = 0;
		int alerts = 0, worstIdx = -1, worstLevel = 0;
		double worstPct = -1.0;
		uint64_t skippedSum = 0;
		uint64_t totalSum = 0;
		bool cleanActive = clean_program_active();
		char cleanCamBuf[256] = {0};
		clean_program_current_cam(cleanCamBuf, sizeof(cleanCamBuf));
		QString cleanCamName = QString::fromUtf8(cleanCamBuf);
		for (int i = 0; i < (int)n && i < (int)cards.size(); i++) {
			Card &c = cards[i];
			QString st;
			QString colName;
			bool active = false;
			switch (rows[i].status) {
			case SR_STATUS_RECORDING:
				if (rows[i].use_buffer) {
					st = QString::fromUtf8("\xE2\x97\x8F BUF");
					colName = IR_GOLD;
					buf++;
				} else {
					st = T("InstantRecord.Dock.Rec");
					colName = IR_RED;
					rec++;
				}
				active = true;
				break;
			case SR_STATUS_ERROR:
				st = T("InstantRecord.Dock.Error");
				colName = "#f0a050";
				err++;
				break;
			default:
				st = T("InstantRecord.Dock.Idle");
				colName = IR_GREEN; /* ready/online, on-brand */
				idle++;
				break;
			}

			/* Camera health: % of frames this camera's encoder skipped. */
			Health hl = row_health(rows[i]);
			if (rows[i].status == SR_STATUS_RECORDING)
				skippedSum += rows[i].skipped_frames;
			if (rows[i].status == SR_STATUS_RECORDING)
				totalSum += rows[i].total_frames;
			if (hl.level >= 1) {
				alerts++;
				if (hl.level > worstLevel || (hl.level == worstLevel && hl.pct > worstPct)) {
					worstLevel = hl.level;
					worstPct = hl.pct;
					worstIdx = i;
				}
			}

			/* Live name + resolution + format: 0x0 becomes the real
			 * resolution once recording, and Global config changes
			 * (container/etc.) show immediately. */
			QString infoTxt = QString::fromUtf8(rows[i].name);
			if (!small) {
				infoTxt += "\n" + QString("%1x%2").arg(rows[i].width).arg(rows[i].height) +
						   QString::fromUtf8(" \xC2\xB7 ") + QString::fromUtf8(rows[i].format);
				if (large) {
					QString el = enc_label(rows[i].encoder);
					if (!el.isEmpty())
						infoTxt += QString::fromUtf8(" \xC2\xB7 ") + el;
				}
			}
			c.info->setText(infoTxt);

			/* Health pill, lag bar and skipped counter. */
			bool live = rows[i].status == SR_STATUS_RECORDING || rows[i].status == SR_STATUS_ERROR;
			c.pct->setVisible(live);
			if (live) {
				c.pct->setText(rows[i].status == SR_STATUS_ERROR ? T("InstantRecord.Dock.Error")
																  : QString::asprintf("%.1f%%", hl.pct));
				c.pct->setStyleSheet(
					QString("background:%1;color:%2;border-radius:8px;padding:2px 8px;font-size:11px;font-weight:700;")
						.arg(level_bg(hl.level), level_color(hl.level)));
			}
			if (large) {
				int barVal = rows[i].status == SR_STATUS_RECORDING ? (int)(hl.pct * 20.0) + 4 : 0;
				c.lagBar->setValue(barVal > 100 ? 100 : barVal);
				c.lagBar->setStyleSheet(
					QString("QProgressBar{background:#242428;border:none;border-radius:3px;}"
							"QProgressBar::chunk{background:%1;border-radius:3px;}")
						.arg(level_color(hl.level)));
				if (rows[i].status == SR_STATUS_RECORDING) {
					c.skipped->setText(
						T("InstantRecord.Health.Skipped").arg(num(rows[i].skipped_frames)) + "\n" +
						T("InstantRecord.Health.OfTotal").arg(num(rows[i].total_frames)));
				} else {
					c.skipped->setText(QStringLiteral("--"));
				}
				c.skipped->setStyleSheet("color:#c9d0d6;");
			}

			/* Blink ONLY the dot while active (no card border). A lagging
			 * camera shows its health colour instead of the status colour. */
			QString baseCol = hl.level >= 1 ? level_color(hl.level) : colName;
			QString dotCol = (active && !blinkOn) ? QString("#44444a") : baseCol;
			c.dot->setStyleSheet(QString("font-size:14px;color:%1;").arg(dotCol));
			c.status->setText(st);
			c.status->setStyleSheet(QString("font-weight:800;color:%1;").arg(colName));
			c.time->setText(format_elapsed(rows[i].elapsed_ns));
			c.clip->setVisible(rows[i].use_buffer && rows[i].status == SR_STATUS_RECORDING);
			/* Block removal while this camera is recording/buffering. */
			c.del->setEnabled(!active);
			c.del->setToolTip(active ? T("InstantRecord.Dock.RemoveBusy") : T("InstantRecord.Dock.Remove"));
			/* Highlight the on-air camera, or a lagging one. Keep the SAME
			 * border width as the default card (1px) so the layout never
			 * shifts — only the colour + a soft tint change. */
			bool onAir = cleanActive && c.name == cleanCamName;
			if (onAir) {
				c.w->setStyleSheet("QFrame#card{background:#13233a;border:1px solid " IR_BLUE
								   ";border-radius:12px;}");
			} else if (hl.level >= 1) {
				c.w->setStyleSheet(QString("QFrame#card{border:1px solid %1;}")
									   .arg(hl.level == 2 ? QString("#7a2a30") : QString("#6b5316")));
			} else {
				c.w->setStyleSheet("");
			}
		}
		liveCams = rec + buf;

		/* Start all turns green while any camera is recording, so it's
		 * obvious the rig is live. */
		bool anyLive = (rec + buf) > 0;
		const QString liveStyle =
			"background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #4ade80,stop:1 #22a35a);border:none;color:#08210f;font-weight:800;";
		startAll->setStyleSheet(anyLive ? liveStyle : QString());
		counters->setText(QString("<span style='color:" IR_RED "'>%1 REC</span>  "
								  "<span style='color:" IR_GOLD "'>%2 BUF</span>  "
								  "<span style='color:" IR_GREEN "'>%3 ready</span>")
							  .arg(rec)
							  .arg(buf)
							  .arg(idle));

		cleanCard->setVisible(cleanActive);
		cleanBtn->setText(cleanActive ? T("InstantRecord.Clean.Stop") : T("InstantRecord.Clean.Start"));
		cleanBtn->setStyleSheet(
			cleanActive
				? "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ef4a44,stop:1 #cc302b);border:none;color:#fff;font-weight:800;"
				: "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #3a8fe0,stop:1 #2472c8);border:none;color:#fff;font-weight:800;");
		if (cleanActive) {
			long long cs = clean_program_elapsed_s();
			cleanTime->setText(QString::asprintf("%02lld:%02lld:%02lld", cs / 3600, (cs % 3600) / 60, cs % 60));
			if (cleanCamName.isEmpty()) {
				cleanCam->setText(QStringLiteral("<span style='color:#8a9099'>") + T("InstantRecord.Clean.NoCam") +
								  QStringLiteral("</span>"));
			} else {
				cleanCam->setText(QStringLiteral("<span style='color:#8a9099'>") + T("InstantRecord.Clean.Now") +
								  QStringLiteral("</span> <span style='color:#fff;font-weight:600'>") +
								  cleanCamName.toHtmlEscaped() + QStringLiteral("</span>"));
			}
			/* Small layout: one row — the camera name replaces the title. */
			cleanCam->setVisible(!small);
			cleanTtl->setText(small && !cleanCamName.isEmpty() ? cleanCamName : T("InstantRecord.Clean.Title"));
		}

		/* ---- layout-dependent chrome ---- */
		subtitle->setVisible(!small);
		counters->setVisible(!small);
		fullBar->setVisible(false); /* legacy buttons, replaced by the mode bar */
		compactBar->setVisible(true);
		statsStrip->setVisible(true);

		/* Header alert pill (small layout only; the banner carries the rest). */
		alertPill->setVisible(small && alerts > 0);
		if (small && alerts > 0) {
			alertPill->setText(alerts == 1 ? T("InstantRecord.Health.AlertCount1")
										   : T("InstantRecord.Health.AlertCountN").arg(alerts));
			alertPill->setStyleSheet(
				QString("background:%1;color:%2;border-radius:8px;padding:2px 8px;font-size:11px;font-weight:700;")
					.arg(level_bg(worstLevel), level_color(worstLevel)));
		}

		/* Alert banner: names the worst camera, with a Reset button. */
		alertBanner->setVisible(alerts > 0);
		if (alerts > 0 && worstIdx >= 0) {
			const struct sr_status_row &w = rows[worstIdx];
			QString cam = QString::fromUtf8(w.name);
			QString msg;
			if (w.status == SR_STATUS_ERROR)
				msg = cam + ": " + T("InstantRecord.Dock.Error");
			else if (small)
				msg = T("InstantRecord.Health.AlertShort").arg(cam, QString::number(worstPct, 'f', 1));
			else if (lm == LAYOUT_MEDIUM)
				msg = T("InstantRecord.Health.Alert").arg(cam, QString::number(worstPct, 'f', 1));
			else
				msg = T("InstantRecord.Health.AlertLong").arg(cam, num(w.skipped_frames), num(w.total_frames));
			const bool red = worstLevel == 2;
			alertBanner->setStyleSheet(QString("QFrame#alert{background:%1;border:1px solid %2;border-radius:12px;}")
										   .arg(red ? QString("#33141a") : QString("#2a2210"),
												red ? QString("#7a2a30") : QString("#6b5316")));
			alertText->setStyleSheet(QString("color:%1;").arg(red ? QString("#f5b5b9") : QString("#f7e3b0")));
			alertIcon->setStyleSheet(QString("font-size:16px;color:%1;").arg(level_color(worstLevel)));
			alertText->setText(msg);
			alertReset->setText(small ? QString::fromUtf8("\xE2\x86\xBB") : T("InstantRecord.Health.Reset"));
		}

		/* Stats strip: same numbers at every size, different shape. */
		{
			double ep = totalSum ? 100.0 * (double)skippedSum / (double)totalSum : 0.0;
			int el = totalSum < 120 ? 0 : (ep >= 2.0 ? 2 : (ep >= 0.5 ? 1 : 0));
			uint32_t lag = obs_get_lagged_frames();
			uint32_t tot = obs_get_total_frames();
			uint32_t dl = lag >= renderBaseLag ? lag - renderBaseLag : 0;
			uint32_t dt = tot >= renderBaseTotal ? tot - renderBaseTotal : 0;
			double rp = dt ? 100.0 * (double)dl / (double)dt : 0.0;
			int rl = rp >= 2.0 ? 2 : (rp >= 0.5 ? 1 : 0);
			QString encPct = QString::asprintf("%.1f%%", ep);
			QString renPct = QString::asprintf("%.1f%%", rp);
			QString diskTxt = disk_free_text(outPath);

			const bool cardsOn = !small;
			for (int k = 0; k < 3; k++)
				statFrame[k]->setVisible(cardsOn);
			statLine->setVisible(small);
			statReset->setText(large ? T("InstantRecord.Health.Reset") : QString::fromUtf8("\xE2\x86\xBB"));

			/* Big number on top; small grey detail underneath (large only). */
			auto statHtml = [](const QString &big, const QString &color, const QString &detail) {
				QString h = QString("<span style='font-size:18px;font-weight:700;color:%1'>%2</span>").arg(color, big);
				if (!detail.isEmpty())
					h += QString("<br><span style='font-size:11px;color:" IR_MUTED "'>%1</span>")
						     .arg(detail.toHtmlEscaped());
				return h;
			};
			if (cardsOn) {
				statTitle[0]->setText(T(large ? "InstantRecord.Health.EncoderLag" : "InstantRecord.Health.EncoderShort"));
				statTitle[1]->setText(T(large ? "InstantRecord.Health.RenderLag" : "InstantRecord.Health.RenderShort"));
				statTitle[2]->setText(T(large ? "InstantRecord.Health.DiskFree" : "InstantRecord.Health.DiskShort"));
				QString encDetail, renDetail, diskDetail;
				if (large) {
					encDetail = T("InstantRecord.Health.Skipped").arg(num(skippedSum)) + " " +
						    T("InstantRecord.Health.OfTotal").arg(num(totalSum));
					renDetail = T("InstantRecord.Health.Skipped").arg(num(dl)) + " " +
						    T("InstantRecord.Health.OfTotal").arg(num(dt));
					diskDetail = QString::fromUtf8("\xC2\xA0");
				}
				statEnc->setText(statHtml(encPct, level_color(el), encDetail));
				statRender->setText(statHtml(renPct, level_color(rl), renDetail));
				statDisk->setText(statHtml(diskTxt, QString(IR_TEXT), diskDetail));
			} else {
				statLine->setText(QString("%1 <b style='color:%2'>%3</b> \xC2\xB7 %4 <b style='color:%5'>%6</b> \xC2\xB7 %7")
							  .arg(T("InstantRecord.Health.EncAbbr"), level_color(el), encPct,
							       T("InstantRecord.Health.RenderShort"), level_color(rl), renPct)
							  .arg(diskTxt));
			}
		}

		/* Compact bottom bar: mode picker + one Start/Stop button. */
		{
			bool running = (curMode == MODE_CLEAN) ? cleanActive : anyLive;
			bool busy = anyLive || cleanActive;
			const QString arrow = QString::fromUtf8(" \xE2\x96\xBE");
			QString modeTxt = T(mode_text_key(curMode, small));
			if (!small)
				modeTxt = T("InstantRecord.Dock.ModeLabel").arg(T(mode_text_key(curMode, false)));
			modeBtn->setText(modeTxt + arrow);
			modeBtn->setEnabled(!busy);
			QString actTip;
			if (curMode == MODE_CLEAN)
				actTip = running ? T("InstantRecord.Clean.Stop") : T("InstantRecord.Clean.Start");
			else
				actTip = running ? T("InstantRecord.Dock.StopAll") : T("InstantRecord.Dock.StartAll");
			QString actTxt = QString::fromUtf8(running ? "\xE2\x96\xA0" : "\xE2\x96\xB6");
			if (!small)
				actTxt += QStringLiteral(" ") + T(running ? "InstantRecord.Dock.Stop" : "InstantRecord.Dock.Start");
			actBtn->setText(actTxt);
			actBtn->setToolTip(actTip);
			actBtn->setStyleSheet(running ? liveStyle : QString());
			saveBtn->setVisible(curMode == MODE_BUFFER);
			saveBtn->setText(small ? QString::fromUtf8("\xE2\x98\x85") : T("InstantRecord.Dock.Save"));
		}
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
				  ";border:1px solid #2a2a2e;border-radius:12px;padding:6px 12px;}"
				  "QPushButton#apply{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd27a,stop:1 #e9a92f);color:#3a2a08;border:none;font-weight:800;}");
		dlg.setWindowTitle(T("InstantRecord.Add.Title"));
		auto *v = new QVBoxLayout(&dlg);

		QListWidget *list = nullptr;
		if (names.empty()) {
			v->addWidget(new QLabel(T("InstantRecord.Add.None")));
		} else {
			v->addWidget(new QLabel(T("InstantRecord.Add.Pick")));
			auto *selRow = new QHBoxLayout();
			auto *selAll = new QPushButton(T("InstantRecord.Add.SelectAll"), &dlg);
			auto *selNone = new QPushButton(T("InstantRecord.Add.SelectNone"), &dlg);
			selRow->addWidget(selAll);
			selRow->addWidget(selNone);
			selRow->addStretch();
			v->addLayout(selRow);

			list = new QListWidget(&dlg);
			for (const QString &nmn : names) {
				auto *it = new QListWidgetItem(nmn, list);
				it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
				it->setCheckState(Qt::Checked);
			}
			v->addWidget(list);

			QListWidget *lw = list;
			connect(selAll, &QPushButton::clicked, &dlg, [lw] {
				for (int i = 0; i < lw->count(); i++)
					lw->item(i)->setCheckState(Qt::Checked);
			});
			connect(selNone, &QPushButton::clicked, &dlg, [lw] {
				for (int i = 0; i < lw->count(); i++)
					lw->item(i)->setCheckState(Qt::Unchecked);
			});
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

protected:
	void resizeEvent(QResizeEvent *e) override
	{
		QWidget::resizeEvent(e);
		int lm = layout_for_width(width());
		if (lm != layoutMode) {
			layoutMode = lm;
			lastSig.clear(); /* rebuild the cards for the new layout */
			refresh();
		}
	}
	void dragEnterEvent(QDragEnterEvent *e) override
	{
		if (e->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist") || e->mimeData()->hasText()) {
			emptyHint->setStyleSheet("border:2px dashed " IR_GOLD ";border-radius:10px;color:" IR_GOLD
						 ";padding:12px;");
			e->acceptProposedAction();
		}
	}
	void dragMoveEvent(QDragMoveEvent *e) override { e->acceptProposedAction(); }
	void dragLeaveEvent(QDragLeaveEvent *) override { emptyHint->setStyleSheet(""); }
	void dropEvent(QDropEvent *e) override
	{
		const QMimeData *m = e->mimeData();
		int added = 0;
		if (m->hasFormat("application/x-qabstractitemmodeldatalist")) {
			QByteArray enc = m->data("application/x-qabstractitemmodeldatalist");
			QDataStream s(&enc, QIODevice::ReadOnly);
			while (!s.atEnd()) {
				int rrow = 0, rcol = 0;
				QMap<int, QVariant> roles;
				s >> rrow >> rcol >> roles;
				/* OBS's source list exposes the source name via the
				 * accessible-text role, not the display role. */
				QString name = roles.value(Qt::AccessibleTextRole).toString();
				if (name.isEmpty())
					name = roles.value(Qt::DisplayRole).toString();
				if (sr_add_filter_to_source(name))
					added++;
			}
		}
		if (added == 0 && m->hasText())
			sr_add_filter_to_source(m->text().trimmed());
		e->acceptProposedAction();
		emptyHint->setStyleSheet("");
		refresh();
	}

private:
	void openGlobalConfig()
	{
		QDialog dlg(this);
		dlg.setStyleSheet("QDialog{background:" IR_BG ";color:" IR_TEXT ";}"
				  "QLabel{color:" IR_TEXT ";}"
				  "QPushButton{background:#161618;color:" IR_TEXT
				  ";border:1px solid #2a2a2e;border-radius:12px;padding:6px 12px;}"
				  "QPushButton#apply{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #ffd27a,stop:1 #e9a92f);color:#3a2a08;border:none;font-weight:800;}");
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
		fmt->addItem(T("InstantRecord.Format.Hybrid"), "hybrid_mp4");
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
		enc->insertItem(0, T("InstantRecord.Global.Encoder.Auto"), QString("auto"));
		enc->setCurrentIndex(0);
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

		/* --- Clean Program settings --- */
		grid->addWidget(new QLabel(T("InstantRecord.Clean.Folder")), r, 0);
		auto *cleanPath = new QLineEdit(&dlg);
		auto *cleanBrowse = new QPushButton(T("InstantRecord.Global.Browse"), &dlg);
		connect(cleanBrowse, &QPushButton::clicked, &dlg, [&] {
			QString d = QFileDialog::getExistingDirectory(&dlg);
			if (!d.isEmpty())
				cleanPath->setText(d);
		});
		grid->addWidget(cleanPath, r, 1);
		grid->addWidget(cleanBrowse, r++, 2);

		grid->addWidget(new QLabel(T("InstantRecord.Clean.Format")), r, 0);
		auto *cleanFmt = new QComboBox(&dlg);
		cleanFmt->addItem(T("InstantRecord.Format.Hybrid"), "hybrid_mp4");
		cleanFmt->addItem("mp4", "mp4");
		cleanFmt->addItem("mkv", "mkv");
		cleanFmt->addItem("mov", "mov");
		grid->addWidget(cleanFmt, r++, 1);

		auto *sidecar = new QCheckBox(T("InstantRecord.Global.Sidecar"), &dlg);
		sidecar->setChecked(true);
		grid->addWidget(sidecar, r++, 1);

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
			cleanPath->setText(QString::fromUtf8(obs_data_get_string(saved, "clean_path")));
			if (obs_data_has_user_value(saved, "clean_container"))
				selectByData(cleanFmt, QString(obs_data_get_string(saved, "clean_container")));
			if (obs_data_has_user_value(saved, "sidecar"))
				sidecar->setChecked(obs_data_get_bool(saved, "sidecar"));
			obs_data_release(saved);
		}

		auto *apply = new QPushButton(T("InstantRecord.Global.ApplyAll"), &dlg);
		apply->setObjectName("apply");
		auto *cancel = new QPushButton(T("InstantRecord.Global.Cancel"), &dlg);
		auto *updates = new QPushButton(T("InstantRecord.Dock.Updates"), &dlg);
		updates->setObjectName("updates");
		updates->setToolTip(T("InstantRecord.Dock.UpdatesTip"));
		updates->setCursor(Qt::PointingHandCursor);
		connect(updates, &QPushButton::clicked, &dlg, [] {
			QDesktopServices::openUrl(
				QUrl("https://github.com/romanrafael12/instant-record/releases/latest"));
		});
		auto *bl = new QHBoxLayout();
		bl->addWidget(updates);
		bl->addStretch();
		bl->addWidget(cancel);
		bl->addWidget(apply);
		grid->addLayout(bl, r, 0, 1, 3);

		connect(cancel, &QPushButton::clicked, &dlg, [&] { dlg.reject(); });
		connect(apply, &QPushButton::clicked, &dlg, [&] {
			QByteArray path = pathEdit->text().toUtf8();
			QByteArray fmtId = fmt->currentData().toString().toUtf8();
			QString encSel = enc->currentData().toString();
			/* "Auto" resolves to a concrete encoder now (adapting to the
			 * machine + the live stream), but we persist "auto" so it
			 * re-resolves next time hardware/stream changes. */
			QByteArray encForFilters =
				(encSel == "auto") ? sr_resolve_auto_encoder().toUtf8() : encSel.toUtf8();
			QByteArray encPref = encSel.toUtf8();

			struct sr_global_config cfg;
			cfg.path = path.isEmpty() ? nullptr : path.constData();
			cfg.rec_format = fmtId.constData();
			cfg.encoder_id = encForFilters.isEmpty() ? nullptr : encForFilters.constData();
			cfg.bitrate = bitrate->value();
			cfg.record_mode = mode->currentData().toInt();
			cfg.isolate_audio = isolate->isChecked() ? 1 : 0;
			cfg.scale_mode = scale->currentData().toInt();
			cfg.fps_divisor = fps->currentData().toInt();
			cfg.use_buffer = buffer->isChecked() ? 1 : 0;
			cfg.also_buffer = 0; /* record+buffer simultaneous removed (unstable) */
			cfg.buffer_seconds = bufSecs->value();
			sr_registry_apply_config(&cfg);

			obs_data_t *d = obs_data_create();
			obs_data_set_string(d, "path", path.constData());
			obs_data_set_string(d, "container", fmtId.constData());
			obs_data_set_string(d, "encoder", encPref.constData());
			obs_data_set_int(d, "bitrate", bitrate->value());
			obs_data_set_int(d, "trigger", mode->currentData().toInt());
			obs_data_set_int(d, "scale", scale->currentData().toInt());
			obs_data_set_int(d, "fps", fps->currentData().toInt());
			obs_data_set_bool(d, "use_buffer", buffer->isChecked());
			obs_data_set_int(d, "buffer_seconds", bufSecs->value());
			obs_data_set_bool(d, "isolate", isolate->isChecked());
			QByteArray cleanPathBytes = cleanPath->text().toUtf8();
			QByteArray cleanFmtBytes = cleanFmt->currentData().toString().toUtf8();
			obs_data_set_string(d, "clean_path", cleanPathBytes.constData());
			obs_data_set_string(d, "clean_container", cleanFmtBytes.constData());
			obs_data_set_bool(d, "sidecar", sidecar->isChecked());
			/* The Replay buffer checkbox and the dock mode button are the same setting. */
			if (curMode != MODE_CLEAN)
				curMode = buffer->isChecked() ? MODE_BUFFER : MODE_RECORD;
			obs_data_set_string(d, "mode", mode_key(curMode));
			outPath = path.isEmpty() ? resolve_out_path() : std::string(path.constData());
			sr_set_sidecar_enabled(sidecar->isChecked() ? 1 : 0);
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
