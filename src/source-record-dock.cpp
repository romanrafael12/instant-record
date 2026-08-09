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
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QFileDialog>

/* Instant Replay brand palette. */
#define IR_BG "#14181d"
#define IR_PANEL "#1b222b"
#define IR_RED "#e0403a"
#define IR_BLUE "#2f8fe0"
#define IR_TEXT "#e7eef5"
#define IR_MUTED "#8aa0b4"

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

class InstantRecordDock : public QWidget {
public:
	explicit InstantRecordDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		setStyleSheet(
			"QWidget{background:" IR_BG ";color:" IR_TEXT ";font-size:12px;}"
			"QLabel#title{font-size:15px;font-weight:700;color:" IR_TEXT ";}"
			"QLabel#brand{color:" IR_RED ";font-weight:800;}"
			"QTableWidget{background:" IR_PANEL ";gridline-color:#2a3542;border:none;}"
			"QHeaderView::section{background:" IR_BG ";color:" IR_MUTED
			";border:none;padding:4px;font-weight:600;}"
			"QPushButton{background:" IR_PANEL ";color:" IR_TEXT
			";border:1px solid #2a3542;border-radius:6px;padding:6px 10px;}"
			"QPushButton:hover{border-color:" IR_BLUE ";}"
			"QPushButton#start{background:" IR_RED ";border:none;font-weight:700;}"
			"QPushButton#save{background:" IR_BLUE ";border:none;font-weight:700;}");

		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(10, 10, 10, 10);

		auto *header = new QHBoxLayout();
		auto *brand = new QLabel("● ", this);
		brand->setObjectName("brand");
		auto *title = new QLabel("Instant Replay — Instant Record", this);
		title->setObjectName("title");
		header->addWidget(brand);
		header->addWidget(title);
		header->addStretch();
		root->addLayout(header);

		table = new QTableWidget(0, 4, this);
		table->setHorizontalHeaderLabels(
			{T("InstantRecord.Dock.Camera"), T("InstantRecord.Dock.Status"),
			 T("InstantRecord.Dock.Time"), QString()});
		table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		table->verticalHeader()->setVisible(false);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionMode(QAbstractItemView::NoSelection);
		root->addWidget(table);

		summary = new QLabel(this);
		summary->setStyleSheet("color:" IR_MUTED ";padding:2px;");
		root->addWidget(summary);

		auto *btns = new QHBoxLayout();
		auto *startAll = new QPushButton(T("InstantRecord.Dock.StartAll"), this);
		startAll->setObjectName("start");
		auto *stopAll = new QPushButton(T("InstantRecord.Dock.StopAll"), this);
		auto *saveAll = new QPushButton(T("InstantRecord.Dock.SaveAll"), this);
		saveAll->setObjectName("save");
		auto *config = new QPushButton(T("InstantRecord.Dock.GlobalConfig"), this);
		btns->addWidget(startAll);
		btns->addWidget(stopAll);
		btns->addWidget(saveAll);
		btns->addStretch();
		btns->addWidget(config);
		root->addLayout(btns);

		connect(startAll, &QPushButton::clicked, this, [] { sr_registry_start_all(); });
		connect(stopAll, &QPushButton::clicked, this, [] { sr_registry_stop_all(); });
		connect(saveAll, &QPushButton::clicked, this, [] { sr_registry_save_all(); });
		connect(config, &QPushButton::clicked, this, [this] { openGlobalConfig(); });

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(500);
		refresh();
	}

private:
	QTableWidget *table;
	QLabel *summary;

	void refresh()
	{
		struct sr_status_row rows[64];
		size_t n = sr_registry_snapshot(rows, 64);

		table->setRowCount((int)n);
		int rec = 0, idle = 0, err = 0;
		for (int i = 0; i < (int)n; i++) {
			QString detail = QString::asprintf("%ux%u · %s", rows[i].width, rows[i].height,
							   rows[i].format);
			auto *name = new QTableWidgetItem(QString("%1\n%2").arg(rows[i].name, detail));

			QString st;
			QColor col;
			switch (rows[i].status) {
			case SR_STATUS_RECORDING:
				if (rows[i].use_buffer) {
					st = QStringLiteral("● BUF");
					col = QColor(IR_BLUE);
				} else {
					st = T("InstantRecord.Dock.Rec");
					col = QColor(IR_RED);
				}
				rec++;
				break;
			case SR_STATUS_ERROR:
				st = T("InstantRecord.Dock.Error");
				col = QColor("#f0b263");
				err++;
				break;
			default:
				st = T("InstantRecord.Dock.Idle");
				col = QColor(IR_MUTED);
				idle++;
				break;
			}
			auto *status = new QTableWidgetItem(st);
			status->setForeground(col);
			auto *time = new QTableWidgetItem(format_elapsed(rows[i].elapsed_ns));

			table->setItem(i, 0, name);
			table->setItem(i, 1, status);
			table->setItem(i, 2, time);

			/* Per-camera Save button, only for buffered + active cams. */
			if (rows[i].use_buffer && rows[i].status == SR_STATUS_RECORDING) {
				auto *btn = new QPushButton(T("InstantRecord.Dock.Save"));
				btn->setObjectName("save");
				int idx = i;
				connect(btn, &QPushButton::clicked, this,
					[idx] { sr_registry_save_index((size_t)idx); });
				table->setCellWidget(i, 3, btn);
			} else {
				table->removeCellWidget(i, 3);
				table->setItem(i, 3, new QTableWidgetItem(QString()));
			}
		}
		table->resizeRowsToContents();
		summary->setText(T("InstantRecord.Dock.Summary").arg(rec).arg(idle).arg(err));
	}

	void openGlobalConfig()
	{
		QDialog dlg(this);
		dlg.setStyleSheet("QDialog{background:" IR_BG ";color:" IR_TEXT ";}"
				  "QLabel{color:" IR_TEXT ";}"
				  "QPushButton{background:" IR_PANEL ";color:" IR_TEXT
				  ";border:1px solid #2a3542;border-radius:6px;padding:6px 10px;}"
				  "QPushButton#apply{background:" IR_BLUE ";border:none;font-weight:700;}");
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

		/* Restore last-applied values. */
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
