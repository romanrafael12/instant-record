/*
Instant Record
Copyright (C) 2026 Rafael Roman <rafael@instanrp.com>

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
#include "source-record-filter.h"

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>

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

static QString format_elapsed(int64_t ns)
{
	if (ns <= 0)
		return QStringLiteral("--");
	qint64 s = ns / 1000000000LL;
	return QString::asprintf("%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
}

/* No Q_OBJECT / no custom slots: we use lambda connects only, so this
 * class needs no moc pass. */
class InstantRecordDock : public QWidget {
public:
	explicit InstantRecordDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		auto *root = new QVBoxLayout(this);

		table = new QTableWidget(0, 3, this);
		table->setHorizontalHeaderLabels({QStringLiteral("Cámara"), QStringLiteral("Estado"),
						  QStringLiteral("Tiempo")});
		table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
		table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		table->verticalHeader()->setVisible(false);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionMode(QAbstractItemView::NoSelection);
		root->addWidget(table);

		summary = new QLabel(this);
		summary->setStyleSheet("color:#8aa0b4;");
		root->addWidget(summary);

		auto *btns = new QHBoxLayout();
		auto *startAll = new QPushButton(QStringLiteral("Iniciar todo"), this);
		auto *stopAll = new QPushButton(QStringLiteral("Detener todo"), this);
		auto *config = new QPushButton(QStringLiteral("Config global…"), this);
		btns->addWidget(startAll);
		btns->addWidget(stopAll);
		btns->addStretch();
		btns->addWidget(config);
		root->addLayout(btns);

		connect(startAll, &QPushButton::clicked, this, [] { sr_registry_start_all(); });
		connect(stopAll, &QPushButton::clicked, this, [] { sr_registry_stop_all(); });
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
				st = QStringLiteral("● REC");
				col = QColor("#ff8f8a");
				rec++;
				break;
			case SR_STATUS_ERROR:
				st = QStringLiteral("⚠ error");
				col = QColor("#f0b263");
				err++;
				break;
			default:
				st = QStringLiteral("○ idle");
				col = QColor("#8b96a1");
				idle++;
				break;
			}
			auto *status = new QTableWidgetItem(st);
			status->setForeground(col);
			auto *time = new QTableWidgetItem(format_elapsed(rows[i].elapsed_ns));

			table->setItem(i, 0, name);
			table->setItem(i, 1, status);
			table->setItem(i, 2, time);
		}
		table->resizeRowsToContents();
		summary->setText(QString::asprintf("%d grabando · %d en espera · %d error", rec, idle, err));
	}

	void openGlobalConfig()
	{
		QDialog dlg(this);
		dlg.setWindowTitle(QStringLiteral("Configuración global — aplicar a todas las cámaras"));
		auto *grid = new QGridLayout(&dlg);
		int r = 0;

		grid->addWidget(new QLabel(QStringLiteral("Carpeta de salida")), r, 0);
		auto *pathEdit = new QLineEdit(&dlg);
		auto *browse = new QPushButton(QStringLiteral("Examinar…"), &dlg);
		connect(browse, &QPushButton::clicked, &dlg, [&] {
			QString d = QFileDialog::getExistingDirectory(&dlg);
			if (!d.isEmpty())
				pathEdit->setText(d);
		});
		grid->addWidget(pathEdit, r, 1);
		grid->addWidget(browse, r++, 2);

		grid->addWidget(new QLabel(QStringLiteral("Contenedor")), r, 0);
		auto *fmt = new QComboBox(&dlg);
		fmt->addItems({QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("mov")});
		grid->addWidget(fmt, r++, 1);

		grid->addWidget(new QLabel(QStringLiteral("Encoder de vídeo")), r, 0);
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

		grid->addWidget(new QLabel(QStringLiteral("Bitrate (Kbps)")), r, 0);
		auto *bitrate = new QSpinBox(&dlg);
		bitrate->setRange(500, 100000);
		bitrate->setSingleStep(500);
		bitrate->setValue(6000);
		grid->addWidget(bitrate, r++, 1);

		grid->addWidget(new QLabel(QStringLiteral("Disparador")), r, 0);
		auto *mode = new QComboBox(&dlg);
		mode->addItem(QStringLiteral("Con la grabación principal de OBS"), 0);
		mode->addItem(QStringLiteral("Manual (atajo)"), 1);
		mode->addItem(QStringLiteral("Siempre (fuente activa)"), 2);
		grid->addWidget(mode, r++, 1);

		grid->addWidget(new QLabel(QStringLiteral("Escala (tope)")), r, 0);
		auto *scale = new QComboBox(&dlg);
		scale->addItem(QStringLiteral("Nativa"), 0);
		scale->addItem(QStringLiteral("1080p"), 1);
		scale->addItem(QStringLiteral("720p"), 2);
		scale->addItem(QStringLiteral("480p"), 3);
		grid->addWidget(scale, r++, 1);

		grid->addWidget(new QLabel(QStringLiteral("Frame rate")), r, 0);
		auto *fps = new QComboBox(&dlg);
		fps->addItem(QStringLiteral("Completo"), 1);
		fps->addItem(QStringLiteral("Mitad"), 2);
		fps->addItem(QStringLiteral("Un cuarto"), 4);
		grid->addWidget(fps, r++, 1);

		auto *isolate = new QCheckBox(QStringLiteral("Aislar audio por fuente"), &dlg);
		isolate->setChecked(true);
		grid->addWidget(isolate, r++, 1);

		auto *apply = new QPushButton(QStringLiteral("Aplicar a todas"), &dlg);
		auto *cancel = new QPushButton(QStringLiteral("Cancelar"), &dlg);
		auto *bl = new QHBoxLayout();
		bl->addStretch();
		bl->addWidget(cancel);
		bl->addWidget(apply);
		grid->addLayout(bl, r, 0, 1, 3);

		connect(cancel, &QPushButton::clicked, &dlg, [&] { dlg.reject(); });
		connect(apply, &QPushButton::clicked, &dlg, [&] {
			QByteArray path = pathEdit->text().toUtf8();
			QByteArray fmtId = fmt->currentText().toUtf8();
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
			sr_registry_apply_config(&cfg);
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
