#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/layout_template.h"
#include "huelith/editing/metadata.h"
#include <QColorDialog>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QSettings>
#include "main_window.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>

namespace huelith::desktop {
using namespace editing;
void EditorController::configure_template_tools() {
    auto* file = window_->menus_->actions().first()->menu();
    auto* templates = file->addMenu(tr("布局与模板"));
    auto create = [this](State state) {
        CanvasSpec spec;
        spec.width = state.size.width();
        spec.height = state.size.height();
        spec.resolution_ppi = state.ppi;
        spec.name = state.name.toStdString();
        if (!window_->create_document(spec))
            throw EditError(tr("无法创建模板文档"));
        canvas()->load_state(std::move(state), false);
        refresh();
    };
    auto* layout = templates->addAction(tr("新建布局模板…"));
    connect(layout, &QAction::triggered, this, [this, create] {
        guarded([&] {
            QDialog dialog(window_);
            dialog.setObjectName("layoutTemplateDialog");
            dialog.setWindowTitle(tr("边框 / 拼图 / 海报"));
            QFormLayout form(&dialog);
            QComboBox preset;
            preset.setObjectName("layoutPreset");
            preset.addItems({tr("四格拼图"), tr("双图排版"), tr("单图边框"), tr("海报")});
            form.addRow(tr("布局"), &preset);
            auto spin = [&](const char* name, const QString& label, int minimum, int maximum,
                            int value) {
                auto* widget = new QSpinBox(&dialog);
                widget->setObjectName(name);
                widget->setRange(minimum, maximum);
                widget->setValue(value);
                form.addRow(label, widget);
                return widget;
            };
            auto* width = spin("layoutWidth", tr("宽度 px"), 32, 16000, 1600);
            auto* height = spin("layoutHeight", tr("高度 px"), 32, 16000, 1200);
            auto* ppi = spin("layoutPpi", tr("分辨率 ppi"), 1, 9600, 300);
            auto* rows = spin("layoutRows", tr("行数"), 1, 10, 2);
            auto* columns = spin("layoutColumns", tr("列数"), 1, 10, 2);
            auto* margin = spin("layoutMargin", tr("边距 px"), 0, 8000, 60);
            auto* gap = spin("layoutGap", tr("间距 px"), 0, 8000, 24);
            auto* caption = spin("layoutCaption", tr("标题区 px"), 0, 8000, 100);
            QLineEdit title(tr("照片集"));
            title.setObjectName("layoutTitle");
            form.addRow(tr("标题"), &title);
            QStringList sources;
            QPushButton photos(tr("选择照片…"));
            QLabel count(tr("未选择：创建可置入图片的相框"));
            form.addRow(&photos);
            form.addRow(&count);
            connect(&photos, &QPushButton::clicked, &dialog, [&] {
                sources = QFileDialog::getOpenFileNames(
                    &dialog, tr("选择拼图照片"), {},
                    tr("图片 (*.png *.jpg *.jpeg *.tif *.tiff *.webp)"));
                count.setText(tr("已选择 %1 张；按照相框顺序置入").arg(sources.size()));
            });
            connect(&preset, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
                rows->setValue(index == 0 ? 2 : 1);
                columns->setValue(index <= 1 ? 2 : 1);
                height->setValue(index == 3 ? 2200 : 1200);
                caption->setValue(index == 3 ? 350 : index == 2 ? 0 : 100);
            });
            QLabel error;
            error.setWordWrap(true);
            error.setObjectName("layoutError");
            form.addRow(&error);
            QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form.addRow(&buttons);
            std::optional<State> result;
            connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
                try {
                    LayoutTemplate parameters;
                    parameters.size = QSize(width->value(), height->value());
                    parameters.ppi = ppi->value();
                    parameters.rows = rows->value();
                    parameters.columns = columns->value();
                    parameters.margin = margin->value();
                    parameters.gap = gap->value();
                    parameters.caption = caption->value();
                    parameters.title = title.text();
                    auto state = create_layout_template(parameters);
                    int index{};
                    for (auto& layer : state.layers)
                        if (layer.frame && index < sources.size()) {
                            const auto source = sources[index++];
                            place_in_frame(state, layer.id, import_image(source), source);
                        }
                    result = std::move(state);
                    dialog.accept();
                } catch (const std::exception& e) {
                    error.setText(QString::fromUtf8(e.what()));
                }
            });
            connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() == QDialog::Accepted && result)
                create(std::move(*result));
        });
    });
    auto* border=templates->addAction(tr("EXIF 照片边框…"));
    connect(border,&QAction::triggered,this,[this,create]{guarded([&]{
        if(!canvas())return;
        canvas()->finish_text(true);canvas()->commit_transform();
        const auto source=canvas()->project().state();
        QDialog dialog(window_);dialog.setObjectName("exifBorderDialog");dialog.setWindowTitle(tr("EXIF 照片边框"));
        dialog.resize(560,400);QFormLayout form(&dialog);
        QSpinBox margin,height,font;margin.setObjectName("exifBorderMargin");height.setObjectName("exifBorderHeight");font.setObjectName("exifBorderFont");
        margin.setRange(0,8000);height.setRange(1,8000);font.setRange(1,2048);
        margin.setValue(std::max(8,source.size.width()/40));height.setValue(std::max(50,source.size.width()/12));font.setValue(std::max(12,source.size.width()/70));
        form.addRow(tr("四周边距 px"),&margin);form.addRow(tr("底部文字区域 px"),&height);form.addRow(tr("字号 px"),&font);
        QPlainTextEdit caption;caption.setObjectName("exifBorderCaption");caption.setPlainText(exif_caption(source.metadata));
        caption.setPlaceholderText(tr("没有 EXIF 时可输入说明；相机、镜头和曝光信息可自由编辑"));form.addRow(tr("照片说明"),&caption);
        QCheckBox use_template(tr("按模板自动填入每张照片的参数")),batch(tr("应用于全部已打开照片（各生成一个副本）"));
        use_template.setObjectName("exifBorderUseTemplate");batch.setObjectName("exifBorderBatch");use_template.setChecked(true);
        QLineEdit pattern(QSettings().value("exifBorder/template","{camera} · {lens} · {exposure} · {copyright}").toString());
        pattern.setObjectName("exifBorderTemplate");pattern.setToolTip(tr("支持 {name} {camera} {lens} {exposure} {copyright}"));
        form.addRow(&use_template);form.addRow(tr("说明模板"),&pattern);form.addRow(&batch);
        const auto refresh_caption=[&]{if(use_template.isChecked())caption.setPlainText(exif_caption_template(pattern.text(),source.metadata,source.name));caption.setReadOnly(use_template.isChecked());};
        connect(&use_template,&QCheckBox::toggled,&dialog,refresh_caption);connect(&pattern,&QLineEdit::textChanged,&dialog,refresh_caption);refresh_caption();
        QImage logo;QPushButton logo_button(tr("选择自有标志 PNG…")),clear_logo(tr("清除标志"));form.addRow(&logo_button,&clear_logo);
        connect(&logo_button,&QPushButton::clicked,&dialog,[&]{try{const auto path=QFileDialog::getOpenFileName(&dialog,tr("自有标志"),{},tr("图片 (*.png *.jpg *.tif)"));if(!path.isEmpty()){logo=import_image(path);logo_button.setText(QFileInfo(path).fileName());}}catch(const std::exception& e){logo_button.setToolTip(QString::fromUtf8(e.what()));}});
        connect(&clear_logo,&QPushButton::clicked,&dialog,[&]{logo={};logo_button.setText(tr("选择自有标志 PNG…"));});
        QColor paper(Qt::white),ink(Qt::black);QPushButton paper_button(tr("边框颜色…")),ink_button(tr("文字颜色…"));
        form.addRow(&paper_button,&ink_button);
        connect(&paper_button,&QPushButton::clicked,&dialog,[&]{const auto value=QColorDialog::getColor(paper,&dialog);if(value.isValid())paper=value;});
        connect(&ink_button,&QPushButton::clicked,&dialog,[&]{const auto value=QColorDialog::getColor(ink,&dialog);if(value.isValid())ink=value;});
        QLabel message(tr("生成新文档，原照片图层和说明文字仍可继续编辑。"));message.setWordWrap(true);form.addRow(&message);
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);std::vector<State> results;
        connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&]{try{
            std::vector<State> originals;
            if(batch.isChecked()){for(int i=0;i<window_->document_count();++i)if(auto* item=window_->canvas_at(i))originals.push_back(item->project().state());}
            else originals.push_back(source);
            std::vector<State> prepared;
            for(const auto& item:originals)prepared.push_back(exif_border(item,margin.value(),height.value(),font.value(),paper,ink,
                use_template.isChecked()?exif_caption_template(pattern.text(),item.metadata,item.name):caption.toPlainText(),logo));
            results=std::move(prepared);QSettings().setValue("exifBorder/template",pattern.text());dialog.accept();
        }catch(const std::exception& e){message.setText(QString::fromUtf8(e.what()));}});
        connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()==QDialog::Accepted)for(auto& result:results)create(std::move(result));
    });});
    auto* save = templates->addAction(tr("保存为模板…"));
    connect(save, &QAction::triggered, this, [this] {
        guarded([&] {
            if (!canvas())
                return;
            canvas()->finish_text(true);
            canvas()->commit_transform();
            const auto path = QFileDialog::getSaveFileName(window_, tr("保存可编辑模板"), {},
                                                           tr("Huelith 模板 (*.huelith)"));
            if (!path.isEmpty())
                save_project(canvas()->project().snapshot(), path);
        });
    });
    auto* load = templates->addAction(tr("从模板新建…"));
    connect(load, &QAction::triggered, this, [this, create] {
        guarded([&] {
            const auto path = QFileDialog::getOpenFileName(window_, tr("从模板新建"), {},
                                                           tr("Huelith 模板 (*.huelith)"));
            if (!path.isEmpty())
                create(load_project(path));
        });
    });
    auto* place = templates->addAction(tr("置入当前相框…"));
    connect(place, &QAction::triggered, this, [this] {
        guarded([&] {
            if (!canvas())
                return;
            const auto path =
                QFileDialog::getOpenFileName(window_, tr("置入相框照片"), {},
                                             tr("图片 (*.png *.jpg *.jpeg *.tif *.tiff *.webp)"));
            if (path.isEmpty())
                return;
            const auto pixels = import_image(path);
            auto* c = canvas();
            c->edit(tr("置入相框"),
                    [&](State& state) { place_in_frame(state, c->active_layer, pixels, path); });
        });
    });
}
} // namespace huelith::desktop
