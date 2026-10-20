#include "huelith/editing/image_storage.h"
#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/brush.h"
#include "main_window.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace huelith::desktop {
void EditorController::show_brush_settings() {
    auto* target = canvas();
    if (!target)
        return;
    guarded([&] {
        using namespace editing;
        auto draft = target->brush_settings();
        auto presets = builtin_brushes();
        const auto builtin_count = presets.size();
        const auto saved =
            QJsonDocument::fromJson(QSettings().value("brush/presets").toByteArray()).array();
        for (const auto& item : saved) {
            try {
                const auto object = item.toObject();
                presets.emplace_back(object.value("name").toString(),
                                     brush_from_json(object.value("settings").toObject()));
            } catch (const std::exception&) {
            } // Ignore an invalid stored preset without discarding valid ones.
        }
        QDialog dialog(window_);
        dialog.setObjectName("brushSettingsDialog");
        dialog.setWindowTitle(tr("画笔设置"));
        dialog.resize(800, 640);
        QVBoxLayout layout(&dialog);
        QHBoxLayout body;
        QListWidget preset_list;
        preset_list.setObjectName("brushPresetList");
        preset_list.setMaximumWidth(195);
        body.addWidget(&preset_list);
        QVBoxLayout settings_layout;
        QHBoxLayout pages_layout;
        QListWidget categories;
        categories.setObjectName("brushCategories");
        categories.addItems(
            {tr("画笔笔尖形状"), tr("形状动态"), tr("散布"), tr("纹理"), tr("传递"), tr("平滑")});
        categories.setMaximumWidth(125);
        QStackedWidget pages;
        pages.setObjectName("brushPages");
        pages_layout.addWidget(&categories);
        pages_layout.addWidget(&pages, 1);
        settings_layout.addLayout(&pages_layout, 1);
        QLabel preview;
        preview.setObjectName("brushStrokePreview");
        preview.setMinimumSize(430, 110);
        settings_layout.addWidget(&preview);
        body.addLayout(&settings_layout, 1);
        layout.addLayout(&body, 1);
        QLabel error;
        error.setObjectName("brushError");
        error.setWordWrap(true);
        layout.addWidget(&error);
        QHBoxLayout management;
        QLineEdit name;
        name.setObjectName("brushPresetName");
        name.setPlaceholderText(tr("新画笔预设名称"));
        name.setMaxLength(80);
        management.addWidget(&name);
        QPushButton save(tr("新建预设")), remove(tr("删除")), import_tip(tr("导入笔尖…")),
            import_presets(tr("导入预设…")), export_presets(tr("导出预设…"));
        save.setObjectName("brushSavePreset");
        remove.setObjectName("brushDeletePreset");
        import_tip.setObjectName("brushImportTip");
        management.addWidget(&save);
        management.addWidget(&remove);
        management.addWidget(&import_tip);
        management.addWidget(&import_presets);
        management.addWidget(&export_presets);
        layout.addLayout(&management);
        QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout.addWidget(&buttons);
        std::vector<std::function<void()>> readers;
        const auto draw_preview = [&] {
            auto shown = draft;
            shown.diameter = std::min(70., shown.diameter);
            QImage coverage(460, 110, QImage::Format_ARGB32_Premultiplied);
            coverage.fill(Qt::transparent);
            BrushStroke stroke;
            stroke.reset(shown, 17);
            for (int x = 35; x <= 425; ++x)
                stroke.add(coverage, QPointF(x, 55 + 20 * std::sin(x * .025)),
                           .65 + .35 * std::sin((x - 35) * 3.141592653589793 / 390));
            QImage image(coverage.size(), QImage::Format_ARGB32_Premultiplied);
            QPainter ink(&coverage);
            ink.setCompositionMode(QPainter::CompositionMode_SourceIn);
            ink.fillRect(coverage.rect(), Qt::black);
            ink.end();
            image.fill(QColor(220, 220, 220));
            QPainter p(&image);
            p.setOpacity(draft.opacity);
            p.drawImage(0, 0, coverage);
            p.end();
            preview.setPixmap(QPixmap::fromImage(image));
        };
        std::array<QFormLayout*, 6> forms{};
        for (auto& form : forms) {
            auto* page = new QWidget;
            form = new QFormLayout(page);
            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            pages.addWidget(scroll);
        }
        const auto number = [&](int page, const char* id, const QString& label,
                                double BrushSettings::* field, double low, double high,
                                double factor = 1.) {
            auto* spin = new QDoubleSpinBox;
            spin->setObjectName(id);
            spin->setRange(low, high);
            spin->setDecimals(factor == 100 ? 0 : 2);
            spin->setSingleStep(1);
            spin->setKeyboardTracking(false);
            spin->setMaximumWidth(85);
            auto* row = new QHBoxLayout;
            auto* slider = new QSlider(Qt::Horizontal);
            slider->setObjectName(QString::fromLatin1(id) + "Slider");
            slider->setRange(qRound(low * 100), qRound(high * 100));
            row->addWidget(slider, 1);
            row->addWidget(spin);
            forms[page]->addRow(label, row);
            readers.emplace_back([&, spin, slider, field, factor] {
                const QSignalBlocker block(spin);
                spin->setValue(draft.*field * factor);
                const QSignalBlocker block_slider(slider);
                slider->setValue(qRound(draft.*field * factor * 100));
            });
            connect(slider, &QSlider::valueChanged, spin,
                    [spin](int value) { spin->setValue(value / 100.); });
            connect(spin, &QDoubleSpinBox::valueChanged, &dialog,
                    [&, slider, field, factor](double value) {
                        const QSignalBlocker block(slider);
                        slider->setValue(qRound(value * 100));
                        draft.*field = value / factor;
                        draw_preview();
                    });
        };
        const auto flag = [&](int page, const char* id, const QString& label,
                              bool BrushSettings::* field) {
            auto* check = new QCheckBox(label);
            check->setObjectName(id);
            forms[page]->addRow(check);
            readers.emplace_back([&, check, field] {
                const QSignalBlocker block(check);
                check->setChecked(draft.*field);
            });
            connect(check, &QCheckBox::toggled, &dialog, [&, field](bool value) {
                draft.*field = value;
                draw_preview();
            });
        };
        auto* tip = new QComboBox;
        tip->setObjectName("brushTip");
        tip->addItems(
            {tr("圆形"), tr("方形"), tr("粉笔"), tr("纤维"), tr("星形"), tr("叶形"), tr("自定义")});
        forms[0]->addRow(tr("笔尖"), tip);
        readers.emplace_back([&] {
            const QSignalBlocker block(tip);
            tip->setCurrentIndex(draft.tip);
        });
        connect(tip, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
            if (index == 6 && draft.custom_tip.isNull()) {
                error.setText(tr("请先导入自定义笔尖图像"));
                const QSignalBlocker block(tip);
                tip->setCurrentIndex(draft.tip);
                return;
            }
            draft.tip = index;
            error.clear();
            draw_preview();
        });
        number(0, "brushDiameter", tr("大小 px"), &BrushSettings::diameter, 1, 5000);
        number(0, "brushHardness", tr("硬度 %"), &BrushSettings::hardness, 0, 100, 100);
        number(0, "brushSpacing", tr("间距 %"), &BrushSettings::spacing, 1, 1000, 100);
        number(0, "brushAngle", tr("角度 °"), &BrushSettings::angle, -180, 180);
        number(0, "brushRoundness", tr("圆度 %"), &BrushSettings::roundness, 1, 100, 100);
        flag(0, "brushFlipX", tr("翻转 X"), &BrushSettings::flip_x);
        flag(0, "brushFlipY", tr("翻转 Y"), &BrushSettings::flip_y);
        number(1, "brushSizeJitter", tr("大小抖动 %"), &BrushSettings::size_jitter, 0, 100, 100);
        number(1, "brushMinimumSize", tr("最小直径 %"), &BrushSettings::minimum_size, 0, 100, 100);
        flag(1, "brushPressureSize", tr("钢笔压力控制大小"), &BrushSettings::pressure_size);
        number(1, "brushAngleJitter", tr("角度抖动 °"), &BrushSettings::angle_jitter, 0, 180);
        flag(1, "brushDirection", tr("角度跟随方向"), &BrushSettings::direction_angle);
        number(1, "brushRoundnessJitter", tr("圆度抖动 %"), &BrushSettings::roundness_jitter, 0,
               100, 100);
        number(2, "brushScatter", tr("散布 %"), &BrushSettings::scatter, 0, 1000, 100);
        flag(2, "brushScatterBoth", tr("两轴"), &BrushSettings::scatter_both);
        auto* count = new QDoubleSpinBox;
        count->setObjectName("brushCount");
        count->setDecimals(0);
        count->setRange(1, 16);
        forms[2]->addRow(tr("数量"), count);
        readers.emplace_back([&] {
            const QSignalBlocker block(count);
            count->setValue(draft.count);
        });
        connect(count, &QDoubleSpinBox::valueChanged, &dialog, [&](double value) {
            draft.count = int(value);
            draw_preview();
        });
        number(2, "brushCountJitter", tr("数量抖动 %"), &BrushSettings::count_jitter, 0, 100, 100);
        auto* texture = new QComboBox;
        texture->setObjectName("brushTexture");
        texture->addItems({tr("无"), tr("纸张"), tr("画布"), tr("颗粒")});
        forms[3]->addRow(tr("纹理"), texture);
        readers.emplace_back([&] {
            const QSignalBlocker block(texture);
            texture->setCurrentIndex(draft.texture);
        });
        connect(texture, &QComboBox::currentIndexChanged, &dialog, [&](int value) {
            draft.texture = value;
            draw_preview();
        });
        number(3, "brushTextureDepth", tr("深度 %"), &BrushSettings::texture_depth, 0, 100, 100);
        number(3, "brushTextureScale", tr("缩放 %"), &BrushSettings::texture_scale, 10, 2000, 100);
        number(4, "brushOpacity", tr("不透明度 %"), &BrushSettings::opacity, 0, 100, 100);
        number(4, "brushFlow", tr("流量 %"), &BrushSettings::flow, 0, 100, 100);
        number(4, "brushFlowJitter", tr("流量抖动 %"), &BrushSettings::flow_jitter, 0, 100, 100);
        flag(4, "brushPressureFlow", tr("钢笔压力控制流量"), &BrushSettings::pressure_flow);
        flag(4, "brushAirbrush", tr("喷枪：按住时累积颜料"), &BrushSettings::airbrush);
        number(5, "brushSmoothing", tr("平滑 %"), &BrushSettings::smoothing, 0, 100, 100);
        flag(5, "brushPulledString", tr("拉绳模式"), &BrushSettings::pulled_string);
        flag(5, "brushCatchUp", tr("描边补齐"), &BrushSettings::catch_up);
        flag(5, "brushCatchUpEnd", tr("描边结束时补齐"), &BrushSettings::catch_up_end);
        flag(5, "brushAdjustZoom", tr("调整缩放"), &BrushSettings::adjust_zoom);
        const auto read = [&] {
            for (const auto& reader : readers)
                reader();
            draw_preview();
        };
        const auto rebuild = [&] {
            const QSignalBlocker block(&preset_list);
            preset_list.clear();
            for (const auto& [title, settings] : presets) {
                QImage icon = brush_tip(settings, 48);
                preset_list.addItem(new QListWidgetItem(QIcon(QPixmap::fromImage(icon)), title));
            }
        };
        const auto serialize = [&] {
            QJsonArray array;
            for (std::size_t i = builtin_count; i < presets.size(); ++i)
                array.append(QJsonObject{{"name", presets[i].first},
                                         {"settings", brush_json(presets[i].second)}});
            return array;
        };
        connect(&categories, &QListWidget::currentRowChanged, &pages,
                &QStackedWidget::setCurrentIndex);
        connect(&preset_list, &QListWidget::currentRowChanged, &dialog, [&](int row) {
            if (row < 0 || row >= int(presets.size()))
                return;
            draft = presets[std::size_t(row)].second;
            name.setText(presets[std::size_t(row)].first);
            read();
        });
        connect(&save, &QPushButton::clicked, &dialog, [&] {
            const auto title = name.text().trimmed();
            if (title.isEmpty()) {
                error.setText(tr("请输入预设名称"));
                return;
            }
            if (presets.size() >= 256) {
                error.setText(tr("预设数量已达上限 256"));
                return;
            }
            presets.emplace_back(title, draft);
            rebuild();
            preset_list.setCurrentRow(int(presets.size()) - 1);
            error.clear();
        });
        connect(&remove, &QPushButton::clicked, &dialog, [&] {
            const int row = preset_list.currentRow();
            if (row < int(builtin_count)) {
                error.setText(tr("内置预设保留；可新建修改后的预设"));
                return;
            }
            presets.erase(presets.begin() + row);
            rebuild();
            error.clear();
        });
        connect(&import_tip, &QPushButton::clicked, &dialog, [&] {
            const auto path =
                QFileDialog::getOpenFileName(&dialog, tr("导入笔尖：黑色绘制、白色透明"), {},
                                             tr("图像 (*.png *.jpg *.jpeg *.bmp)"));
            if (path.isEmpty())
                return;
            QImageReader reader(path);
            const auto size = reader.size();
            if (!size.isValid() || qint64(size.width()) * size.height() > huelith::editing::maximum_image_pixels) {
                error.setText(tr("无法读取笔尖或图像超过 5.12 亿像素"));
                return;
            }
            auto input = reader.read();
            if (input.isNull()) {
                error.setText(reader.errorString());
                return;
            }
            input = input.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_ARGB32);
            QImage mask(input.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < input.height(); ++y) {
                const auto* row = reinterpret_cast<const QRgb*>(input.constScanLine(y));
                auto* out = mask.scanLine(y);
                for (int x = 0; x < input.width(); ++x)
                    out[x] = uchar((255 - qGray(row[x])) * qAlpha(row[x]) / 255);
            }
            draft.custom_tip = mask;
            draft.tip = 6;
            error.clear();
            read();
        });
        connect(&import_presets, &QPushButton::clicked, &dialog, [&] {
            const auto path = QFileDialog::getOpenFileName(&dialog, tr("导入 Huelith 画笔预设"), {},
                                                           tr("画笔预设 (*.huelithbrush)"));
            if (path.isEmpty())
                return;
            try {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
                    throw EditError(tr("预设文件无法读取或过大"));
                QJsonParseError parse;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
                if (parse.error != QJsonParseError::NoError || !doc.isArray())
                    throw EditError(tr("预设文件格式无效"));
                auto next = presets;
                for (const auto& value : doc.array()) {
                    const auto item = value.toObject();
                    const auto title = item.value("name").toString().trimmed();
                    if (title.isEmpty() || title.size() > 80 || next.size() >= 256 ||
                        !item.value("settings").isObject())
                        throw EditError(tr("预设名称、参数或数量无效"));
                    next.emplace_back(title, brush_from_json(item.value("settings").toObject()));
                }
                presets = std::move(next);
                rebuild();
                error.clear();
            } catch (const std::exception& e) {
                error.setText(QString::fromUtf8(e.what()));
            }
        });
        connect(&export_presets, &QPushButton::clicked, &dialog, [&] {
            const auto path = QFileDialog::getSaveFileName(&dialog, tr("导出自定义画笔预设"), {},
                                                           tr("画笔预设 (*.huelithbrush)"));
            if (path.isEmpty())
                return;
            QSaveFile file(path);
            const auto bytes = QJsonDocument(serialize()).toJson();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                !file.commit())
                error.setText(tr("无法写入画笔预设"));
            else
                error.clear();
        });
        connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        rebuild();
        categories.setCurrentRow(0);
        read();
        if (dialog.exec() != QDialog::Accepted)
            return;
        target->set_brush_settings(draft);
        QSettings storage;
        storage.setValue("brush/current",
                         QJsonDocument(brush_json(draft)).toJson(QJsonDocument::Compact));
        storage.setValue("brush/presets",
                         QJsonDocument(serialize()).toJson(QJsonDocument::Compact));
        refresh_tools();
    });
}
} // namespace huelith::desktop
