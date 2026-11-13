#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/layout_regions.h"
#include "main_window.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <atomic>
#include <cmath>
namespace huelith::desktop {
using namespace editing;
namespace {
QJsonArray parse(const QByteArray& data) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(data, &error);
    if (!doc.isArray() || doc.array().isEmpty() || doc.array().size() > 256)
        throw EditError(QObject::tr("动作须包含 1–256 个步骤。%1").arg(error.errorString()));
    return doc.array();
}
void play(State& state, QUuid active, const QJsonArray& steps, const QJsonObject& vars = {}) {
    for (int i = 0; i < steps.size(); ++i) {
        const auto step = steps[i].toObject();
        const auto op = step.value("op").toString();
        auto fail = [&](QString reason) {
            throw EditError(QObject::tr("步骤 %1（%2）：%3").arg(i + 1).arg(op, reason));
        };
        const QString cond = step.value("if").toString();
        if (cond == "landscape" && state.size.width() <= state.size.height())
            continue;
        if (cond == "portrait" && state.size.height() <= state.size.width())
            continue;
        if (!cond.isEmpty() && cond != "landscape" && cond != "portrait")
            fail("未知条件");
        auto value = [&](QString key) {
            auto v = step.value(key);
            if (v.isString() && v.toString().startsWith('$')) {
                auto name = v.toString().mid(1);
                if (!vars.contains(name))
                    fail("缺少变量：" + name);
                v = vars.value(name);
            }
            return v;
        };
        auto number = [&](QString key, double lo, double hi) {
            auto v = value(key);
            if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < lo ||
                v.toDouble() > hi)
                fail("参数超出范围：" + key);
            return v.toDouble();
        };
        if (op == "stop")
            fail(step.value("message").toString("已停止，未应用此动作"));
        if (op == "resize") {
            auto w = number("width", 1, 65536), h = number("height", 1, 65536);
            resize_document(state, {qRound(w), qRound(h)},
                            step.contains("ppi") ? number("ppi", 1, 100000) : state.ppi);
            continue;
        }
        auto* l = find_layer(state.layers, active);
        if (!l || layer_locked(state.layers, active))
            fail("活动图层不存在或已锁定");
        if (op == "opacity")
            l->opacity = number("value", 0, 1);
        else if (op == "exposure")
            l->exposure = number("value", -16, 16);
        else if (op == "rename") {
            auto v = value("value");
            if (!v.isString() || v.toString().trimmed().isEmpty())
                fail("缺少名称");
            l->name = v.toString();
        } else
            fail("不支持此操作");
    }
}
} // namespace
void EditorController::actions_dialog(bool batch) {
    auto* c = canvas();
    if (!batch && !c)
        return;
    QDialog dlg(window_);
    dlg.setWindowTitle(batch ? tr("动作批处理") : tr("编辑与播放动作"));
    QVBoxLayout layout(&dlg);
    QLabel help(tr("支持 opacity（0–1）、exposure、resize、rename、stop。可加 if: landscape 或 "
                   "portrait。批处理可用 $变量。"));
    help.setWordWrap(true);
    layout.addWidget(&help);
    QPlainTextEdit editor;
    editor.setObjectName("actionSteps");
    editor.setPlainText(
        QSettings().value("actions/steps", "[{\"op\":\"opacity\",\"value\":1}]").toString());
    layout.addWidget(&editor);
    QPushButton load(tr("加载动作文件…")), save(tr("保存动作文件…"));
    QHBoxLayout files;
    files.addWidget(&load);
    files.addWidget(&save);
    layout.addLayout(&files);
    load.setEnabled(QSettings().value("extensions/actions", true).toBool());
    auto readFile = [](QString path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > 1048576)
            throw EditError(QObject::tr("无法读取或文件超过 1 MiB"));
        return f.readAll();
    };
    connect(&load, &QPushButton::clicked, &dlg, [&] {
        guarded([&] {
            auto p = QFileDialog::getOpenFileName(&dlg, tr("动作文件"), {}, "JSON (*.json)");
            if (p.isEmpty())
                return;
            auto data = readFile(p);
            parse(data);
            editor.setPlainText(QString::fromUtf8(data));
        });
    });
    connect(&save, &QPushButton::clicked, &dlg, [&] {
        guarded([&] {
            auto data = editor.toPlainText().toUtf8();
            parse(data);
            auto p = QFileDialog::getSaveFileName(&dlg, tr("保存动作"), {}, "JSON (*.json)");
            if (p.isEmpty())
                return;
            QSaveFile f(p);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
                throw EditError(f.errorString());
        });
    });
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons.button(QDialogButtonBox::Ok)->setText(batch ? tr("选择批处理输入…") : tr("播放"));
    layout.addWidget(&buttons);
    QJsonArray steps;
    connect(&buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        guarded([&] {
            steps = parse(editor.toPlainText().toUtf8());
            dlg.accept();
        });
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QSettings().setValue("actions/steps", editor.toPlainText());
    if (!batch) {
        c->edit(tr("播放动作"), [&](State& s) { play(s, c->active_layer, steps); });
        return;
    }
    auto paths =
        QFileDialog::getOpenFileNames(window_, tr("选择图片或数据集 JSON"), {},
                                      tr("图片或数据集 (*.png *.jpg *.jpeg *.tif *.tiff *.json)"));
    if (paths.isEmpty())
        return;
    auto dir = QFileDialog::getExistingDirectory(window_, tr("选择输出目录"));
    if (dir.isEmpty())
        return;
    QList<QPair<QString, QJsonObject>> inputs;
    for (auto path : paths) {
        if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive)) {
            inputs.append({path, {}});
            continue;
        }
        if (!QSettings().value("extensions/actions", true).toBool())
            throw EditError(tr("外部数据集已在首选项中停用"));
        auto dataset = QJsonDocument::fromJson(readFile(path));
        if (!dataset.isArray() || dataset.array().isEmpty())
            throw EditError(tr("数据集格式：[{\"file\":\"图片名\",\"variables\":{…}}]"));
        for (auto row : dataset.array()) {
            auto object = row.toObject();
            QString name = object.value("file").toString();
            if (name.isEmpty() || QFileInfo(name).fileName() != name)
                throw EditError(tr("数据集图片应与 JSON 在同一目录"));
            inputs.append(
                {QFileInfo(path).dir().filePath(name), object.value("variables").toObject()});
        }
    }
    QProgressDialog progress(tr("正在处理图片…"), tr("取消批处理"), 0, inputs.size(), window_);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    int done = 0;
    QStringList errors;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    connect(&progress, &QProgressDialog::canceled, &progress, [cancelled] { *cancelled = true; });
    for (int i = 0; i < inputs.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled())
            break;
        const auto [path, vars] = inputs[i];
        progress.setLabelText(tr("正在处理 %1 / %2：%3")
                                  .arg(i + 1)
                                  .arg(inputs.size())
                                  .arg(QFileInfo(path).fileName()));
        QFutureWatcher<QString> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([path, vars, steps, dir, i, cancelled]() -> QString {
            try {
                if (*cancelled)
                    return {};
                State s;
                s.name = QFileInfo(path).completeBaseName();
                Layer l;
                l.name = s.name;
                l.pixels = import_image(path);
                if (*cancelled)
                    return {};
                s.size = l.pixels.size();
                s.layers = {l};
                play(s, l.id, steps, vars);
                if (*cancelled)
                    return {};
                QString output = QDir(dir).filePath(QString("%1-%2.png").arg(s.name).arg(i + 1));
                if (QFileInfo::exists(output))
                    throw EditError(QObject::tr("输出已存在"));
                RenderOptions options;
                options.cancelled = cancelled.get();
                auto image =
                    display_image(render(s, options)).convertToFormat(QImage::Format_RGBA8888);
                if (*cancelled)
                    return {};
                QSaveFile file(output);
                if (!file.open(QIODevice::WriteOnly))
                    throw EditError(file.errorString());
                QImageWriter writer(&file, "png");
                if (!writer.write(image))
                    throw EditError(writer.errorString());
                // Cancellation during PNG encoding discards the temporary output.
                if (*cancelled) {
                    file.cancelWriting();
                    return {};
                }
                if (!file.commit())
                    throw EditError(file.errorString());
                return QString();
            } catch (const std::exception& e) {
                return QString::fromUtf8(e.what());
            }
        }));
        loop.exec();
        if (*cancelled)
            break;
        auto failure = watcher.result();
        if (failure.isEmpty())
            ++done;
        else
            errors.append(QFileInfo(path).fileName() + ": " + failure);
    }
    progress.close();
    QMessageBox::information(window_, tr("批处理"),
                             tr("已输出 %1 / %2 张。").arg(done).arg(inputs.size()) + "\n" +
                                 errors.join("\n"));
}
} // namespace huelith::desktop
