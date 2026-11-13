#include "canvas_view.h"
#include "editor_controller.h"
#include "huelith/editing/photo_operations.h"
#include "main_window.h"
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHttpMultiPart>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebSocket>
#include <QtConcurrentRun>
#include <climits>
#include <cmath>
#include <cstring>

namespace huelith::desktop {
namespace {
using namespace editing;
QJsonObject jsonObject(QByteArray bytes) {
    QJsonParseError e;
    auto d = QJsonDocument::fromJson(bytes, &e);
    if (!d.isObject())
        throw EditError(QObject::tr("JSON 对象无效：%1").arg(e.errorString()));
    return d.object();
}
QJsonObject readJson(QString path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024)
        throw EditError(QObject::tr("工作流文件不可读或超过 4 MiB"));
    return jsonObject(f.readAll());
}
struct Slot {
    QString node, key, role, kind, white;
    QComboBox* source{};
};
class WorkflowDialog final : public QWidget {
    QPointer<CanvasView> canvas;
    QNetworkAccessManager network;
    QWebSocket progressSocket;
    QString nodeProgress;
    QLineEdit address, token, search;
    QComboBox templates;
    QSize serviceSize;
    QLabel status, thumbnail;
    QString jobAddress;
    std::shared_ptr<std::atomic_bool> cancelled;
    QPushButton load{tr("导入工作流…")}, check{tr("检查服务与依赖")}, generate{tr("生成")}, cancel{tr("取消任务")},
        apply{tr("应用候选")}, preview{tr("预览输入")}, retry{tr("重新获取任务结果")};
    QComboBox candidates;
    QWidget settings;
    QVBoxLayout options{&settings};
    QScrollArea scroll;
    QJsonObject graph, manifest, info, submitted, frozenGraph, frozenManifest;
    QList<Slot> slots_;
    const QDir library{QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                       "/workflow-library"};
    QMap<QString, QJsonObject> parameterCache;
    QString workflowKey;
    QList<QPair<QString, QByteArray>> uploads;
    QString job, client = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 ticket{}, revision{};
    bool busy{};
    QElapsedTimer elapsed;
    std::vector<QUuid> sources;
    QImage base, mask, linearBase;
    QMap<QString, QImage> inputSnapshots;
    QJsonArray sourcePlan;
    QList<QImage> images;
    QList<QJsonObject> downloads;
    QStringList issues;
    QSpinBox* margins[4]{};
    QRect outputBounds;
    QSize originalSize;
    bool expand{}, matte{};
    static QImage padded(const QImage& im, bool range, QRect outputBounds) {
        if (im.size() == outputBounds.size())
            return im;
        QImage output(outputBounds.size(), im.format());
        output.fill(range ? Qt::white : Qt::transparent);
        output.setColorSpace(im.colorSpace());
        const int dx = -outputBounds.x(), dy = -outputBounds.y(), bytes = im.depth() / 8;
        for (int y = 0; y < im.height(); ++y)
            std::memcpy(output.scanLine(y + dy) + dx * bytes, im.constScanLine(y),
                        im.width() * bytes);
        return output;
    }
    std::function<bool()> isCurrent;
    std::function<void()> refresh;
    QUrl url(QString path) const {
        QUrl u(address.text().trimmed());
        if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() || u.hasQuery() ||
            u.hasFragment() || (u.scheme() != "https" && u.scheme() != "http"))
            throw EditError(tr("请填写 HTTP(S) 服务地址，认证信息单独填写"));
        QString prefix = u.path();
        if (prefix.endsWith('/'))
            prefix.chop(1);
        u.setPath(prefix + "/" + path);
        return u;
    }
    QNetworkRequest req(QUrl u) const {
        QNetworkRequest r(u);
        r.setTransferTimeout(45000);
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
        if (!token.text().isEmpty())
            r.setRawHeader("Authorization", "Bearer " + token.text().toUtf8());
        return r;
    }
    void finished(bool running) {
        busy = running;
        if (!running)
            progressSocket.abort();
        settings.setEnabled(!running);
        address.setEnabled(!running);
        token.setEnabled(!running);
        templates.setEnabled(!running);
        load.setEnabled(!running);
        check.setEnabled(!running);
        generate.setEnabled(!running && !info.isEmpty() && issues.isEmpty() && !graph.isEmpty());
        preview.setEnabled(!running && !graph.isEmpty());
        retry.setEnabled(!running && !job.isEmpty() && jobAddress == address.text() &&
                         images.isEmpty());
        cancel.setEnabled(running);
        apply.setEnabled(!running && !images.empty());
    }
    void error(QString s) {
        ++ticket;
        if (busy) {
            images.clear();
            candidates.clear();
            uploads.clear();
        }
        finished(false);
        status.setText(s.left(1300));
    }
    template <class F> void guarded(F f) {
        try {
            f();
        } catch (const std::exception& e) {
            error(QString::fromUtf8(e.what()));
        }
    }
    void receive(QNetworkReply* reply, std::function<void(QByteArray)> cb,
                 qint64 cap = 8 * 1024 * 1024) {
        auto serial = ticket;
        reply->setReadBufferSize(128 * 1024);
        auto bytes = std::make_shared<QByteArray>();
        auto drain = [reply, bytes, cap] {
            auto chunk = reply->readAll();
            if (chunk.size() > cap - bytes->size()) {
                reply->setProperty("oversized", true);
                reply->abort();
                return;
            }
            bytes->append(chunk);
        };
        connect(reply, &QIODevice::readyRead, reply, drain);
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater();
            if (serial != ticket)
                return;
            guarded([&] {
                if (reply->isOpen() && reply->bytesAvailable())
                    drain();
                if (reply->property("oversized").toBool())
                    throw EditError(tr("服务响应超过大小限制"));
                int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (reply->error() != QNetworkReply::NoError || http < 200 || http >= 300) {
                    auto e = QJsonDocument::fromJson(*bytes).object();
                    QString detail = e.value("error").toObject().value("message").toString();
                    if (!e.value("node_errors").toObject().isEmpty())
                        detail +=
                            " " + QString::fromUtf8(QJsonDocument(e.value("node_errors").toObject())
                                                        .toJson(QJsonDocument::Compact))
                                      .left(800);
                    throw EditError(tr("服务请求失败（%1）：%2 %3")
                                        .arg(http)
                                        .arg(reply->errorString(), detail));
                }
                cb(*bytes);
            });
        });
    }
    void put(QJsonObject& g, QString n, QString k, QJsonValue v) {
        auto node = g[n].toObject(), in = node["inputs"].toObject();
        in[k] = v;
        node["inputs"] = in;
        g[n] = node;
    }
    void rebuild() {
        while (auto* item = options.takeAt(0)) {
            delete item->widget();
            delete item;
        }
        slots_.clear();
        issues.clear();
        auto geometry = manifest["geometry"].toObject();
        auto mode = geometry["mode"].toString("sameAsInput");
        expand = mode == "outpaint";
        matte = manifest["resultMode"] == "layerMask";
        if (matte) {
            auto* note = new QLabel(
                tr("用当前图层原始边缘与粗蒙版生成可编辑的新蒙版。服务需要 ComfyUI-Image-Matting "
                   "节点和 ViTMatte 权重；请先在服务所在机器准备好。"));
            note->setWordWrap(true);
            options.addWidget(note);
        }
        if (!expand && mode != "sameAsInput")
            issues.append(tr("不支持的输出坐标模式：") + mode);
        if (expand) {
            auto* group = new QGroupBox(tr("扩图边距（像素）"));
            auto* form = new QFormLayout(group);
            int idx = 0;
            for (auto k : {"left", "top", "right", "bottom"}) {
                auto* b = new QSpinBox;
                b->setRange(0, 8192);
                b->setValue(geometry[k].toInt(128));
                margins[idx] = b;
                form->addRow(QStringList{tr("左"), tr("上"), tr("右"), tr("下")}.at(idx++), b);
            }
            options.addWidget(group);
        }
        QSet<QString> bound;
        int primaries = 0, masks = 0, editMasks = 0;
        for (auto el : manifest["inputs"].toArray()) {
            auto v = el.toObject();
            Slot slot{v["node"].toString(), v["input"].toString(), v["role"].toString(),
                      v["kind"].toString(), v["whiteMeans"].toString("edit")};
            auto input = graph[slot.node].toObject()["inputs"].toObject();
            if (!input.contains(slot.key) || !input[slot.key].isString() ||
                bound.contains(slot.node + "/" + slot.key))
                throw EditError(tr("工作流输入绑定无效或重复"));
            bound.insert(slot.node + "/" + slot.key);
            auto* b = new QComboBox;
            slot.source = b;
            b->setObjectName("comfySource_" + slot.role);
            if (slot.kind == "image") {
                primaries += slot.role == "primary";
                b->addItems({tr("当前图层"), tr("选中图层"), tr("所有可见图层")});
                if (matte && slot.role == "primary") {
                    b->setCurrentIndex(0);
                    b->setEnabled(false);
                }
            } else if (slot.kind == "mask") {
                ++masks;
                editMasks += slot.role == "editMask";
                b->addItems({tr("当前选区"), tr("全图"), tr("当前图层蒙版")});
                if (matte)
                    b->setCurrentIndex(2);
                else if (templates.currentIndex() == 3)
                    b->setCurrentIndex(1);
                if (expand) {
                    b->addItem(tr("仅扩展区域"));
                    b->setCurrentIndex(3);
                }
            } else {
                delete b;
                throw EditError(tr("不支持的输入种类：") + slot.kind);
            }
            options.addWidget(new QLabel(slot.role + " · " + slot.node + "/" + slot.key));
            options.addWidget(b);
            slots_.append(slot);
        }
        if (primaries != 1)
            issues.append(tr("工作流须声明一个 primary 图像输入"));
        if (expand && !masks)
            issues.append(tr("扩图需要蒙版输入"));
        if (editMasks > 1 || (masks > 1 && editMasks != 1))
            issues.append(tr("多个蒙版须指定唯一 editMask 作为最终修改范围，其余作为参考蒙版"));
        if (slots_.size() > 30)
            issues.append(tr("输入槽最多 30 个"));
        bool hasFinal = false;
        for (auto v : manifest["outputs"].toArray()) {
            auto o = v.toObject();
            if (o["role"] == "final" && o["kind"] == "image" &&
                graph.contains(o["node"].toString()))
                hasFinal = true;
        }
        if (!hasFinal)
            issues.append(tr("缺少有效的最终图像输出绑定"));
        for (auto n : graph.keys()) {
            auto node = graph[n].toObject();
            QString type = node["class_type"].toString();
            auto inputs = node["inputs"].toObject();
            if (type.isEmpty() || !node["inputs"].isObject())
                throw EditError(tr("请选择 API 格式工作流，节点无效：") + n);
            if (!info.isEmpty() && !info.contains(type))
                issues.append(tr("缺少节点：") + type);
            auto spec = info[type].toObject()["input"].toObject();
            auto fields = spec["required"].toObject();
            auto optional = spec["optional"].toObject();
            for (auto k : optional.keys())
                fields[k] = optional[k];
            auto* box = new QGroupBox(node["_meta"].toObject()["title"].toString(type) + " · " + n);
            auto* form = new QFormLayout(box);
            for (auto k : fields.keys()) {
                if (inputs.contains(k))
                    continue;
                auto definition = fields[k].toArray();
                auto typeName = definition.at(0);
                auto defaults = definition.at(1).toObject();
                auto initial = defaults.value("default");
                if (initial.isUndefined() && typeName.isArray() && !typeName.toArray().empty())
                    initial = typeName.toArray().first();
                if (initial.isUndefined()) {
                    if (!optional.contains(k))
                        issues.append(tr("缺少必填输入：%1/%2").arg(type, k));
                    continue;
                }
                auto* enable = new QCheckBox(tr("启用可选参数：") + k);
                enable->setObjectName("comfyOptional_" + n + "_" + k);
                if (!optional.contains(k)) {
                    inputs[k] = initial;
                    put(graph, n, k, initial);
                    continue;
                }
                form->addRow(enable);
                connect(enable, &QCheckBox::toggled, this, [this, n, k, initial](bool on) {
                    if (!on)
                        return;
                    put(graph, n, k, initial);
                    QTimer::singleShot(0, this, [this] { guarded([this] { rebuild(); }); });
                });
            }
            QString words = box->title();
            for (auto k : inputs.keys()) {
                auto v = inputs[k];
                if (bound.contains(n + "/" + k))
                    continue;
                QString label = k;
                for (auto p : manifest["parameters"].toArray()) {
                    auto m = p.toObject();
                    if (m["node"] == n && m["input"] == k)
                        label = m["label"].toString(k);
                }
                words += " " + label + " " + k;
                auto def = fields[k].toArray();
                auto t = def.at(0);
                auto limits = def.at(1).toObject();
                if (v.isArray() && v.toArray().size() == 2 && v.toArray()[0].isString() &&
                    v.toArray()[1].isDouble() && graph.contains(v.toArray()[0].toString())) {
                    form->addRow(label,
                                 new QLabel(tr("来自节点 %1").arg(v.toArray()[0].toString())));
                    continue;
                }
                auto setter = [=, this](QJsonValue value) { put(graph, n, k, value); };
                if (t.isArray() || t.toString() == "COMBO") {
                    auto* b = new QComboBox;
                    b->setObjectName("comfyParam_" + n + "_" + k);
                    auto list = t.isArray() ? t.toArray() : limits["options"].toArray();
                    for (auto item : list)
                        b->addItem(item.toVariant().toString(), item.toVariant());
                    int ix = b->findData(v.toVariant());
                    if (ix < 0) {
                        b->insertItem(0, tr("缺失：") + v.toVariant().toString(), v.toVariant());
                        b->setProperty("missing", true);
                        ix = 0;
                    }
                    b->setCurrentIndex(ix);
                    form->addRow(label, b);
                    connect(b, &QComboBox::currentIndexChanged, this, [b, setter](int index) {
                        setter(QJsonValue::fromVariant(b->itemData(index)));
                    });
                } else if ((t.toString() == "BOOLEAN" || t.isUndefined()) && v.isBool()) {
                    auto* b = new QCheckBox;
                    b->setChecked(v.toBool());
                    form->addRow(label, b);
                    connect(b, &QCheckBox::toggled, this, setter);
                } else if ((t.toString() == "FLOAT" || t.toString() == "INT" || t.isUndefined()) &&
                           v.isDouble()) {
                    auto* b = new QLineEdit(
                        QString::fromUtf8(
                            QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact))
                            .sliced(1)
                            .chopped(1));
                    b->setObjectName("comfyParam_" + n + "_" + k);
                    form->addRow(label, b);
                    connect(b, &QLineEdit::textChanged, this, [=](QString value) {
                        auto arr = QJsonDocument::fromJson("[" + value.toUtf8() + "]").array();
                        auto number = arr.at(0);
                        bool ok =
                            arr.size() == 1 && number.isDouble() &&
                            number.toDouble() >= limits["min"].toDouble(-1e300) &&
                            number.toDouble() <= limits["max"].toDouble(1e300) &&
                            (t.toString() != "INT" || number.toInteger(LLONG_MIN) != LLONG_MIN);
                        b->setProperty("invalid", !ok);
                        if (ok)
                            setter(number);
                    });
                    const double lo = limits["min"].toDouble(), hi = limits["max"].toDouble();
                    if (limits.contains("min") && limits.contains("max") && hi > lo &&
                        hi - lo <= 1000000 && k != "seed" && k != "noise_seed") {
                        auto* track = new QSlider(Qt::Horizontal);
                        track->setRange(0, 1000);
                        track->setValue(
                            qRound(std::clamp((v.toDouble() - lo) / (hi - lo), 0., 1.) * 1000));
                        form->addRow(track);
                        connect(track, &QSlider::valueChanged, this,
                                [b, lo, hi, t, limits](int value) {
                                    double number = lo + (hi - lo) * value / 1000.;
                                    double step =
                                        limits["step"].toDouble(t.toString() == "INT" ? 1. : .01);
                                    if (step > 0)
                                        number = std::clamp(
                                            lo + std::round((number - lo) / step) * step, lo, hi);
                                    if (t.toString() == "INT")
                                        number = std::round(number);
                                    b->setText(QString::number(number, 'g', 12));
                                });
                        connect(b, &QLineEdit::textChanged, track, [track, lo, hi](QString text) {
                            bool ok;
                            double value = text.toDouble(&ok);
                            if (!ok)
                                return;
                            QSignalBlocker blocker(track);
                            track->setValue(
                                qRound(std::clamp((value - lo) / (hi - lo), 0., 1.) * 1000));
                        });
                    }
                } else if ((t.toString() == "STRING" || t.isUndefined()) && v.isString()) {
                    auto* b = new QPlainTextEdit(v.toString());
                    b->setMaximumHeight(72);
                    form->addRow(label, b);
                    connect(b, &QPlainTextEdit::textChanged, this,
                            [b, setter] { setter(b->toPlainText()); });
                } else {
                    issues.append(tr("缺少控件：%1/%2").arg(type, k));
                    form->addRow(label, new QLabel(tr("尚未支持")));
                }
            }
            box->setCheckable(true);
            connect(box, &QGroupBox::toggled, box, [box](bool on) {
                for (auto* w : box->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                    w->setVisible(on);
            });
            box->setProperty("words", words);
            options.addWidget(box);
        }
        finished(false);
        status.setText(
            issues.isEmpty()
                ? (info.isEmpty() ? tr("工作流已载入，请检查服务") : tr("服务与工作流已就绪"))
                : issues.join("；") + tr("。请在当前服务上补齐依赖后重新检查。"));
    }
    static QImage inputImage(State s, int mode, bool primary, QUuid active,
                             const std::vector<QUuid>& selected, std::vector<QUuid>& sources,
                             const std::atomic_bool* cancelled, bool unmask) {
        auto all = s.layers;
        std::vector<QUuid> ids = mode == 0 ? std::vector<QUuid>{active} : selected;
        if (mode != 2) {
            ids = root_targets(s, ids);
            if (ids.empty())
                throw EditError(tr("没有选中的图层"));
            s.layers.clear();
        }
        int previous = -1;
        for (int i = 0; i < int(all.size()); ++i) {
            auto l = all[i];
            if (mode == 2 && !l.visible)
                continue;
            if (mode != 2) {
                if (std::find(ids.begin(), ids.end(), l.id) == ids.end())
                    continue;
                if (l.clipped || l.blend != Blend::normal || l.kind == LayerKind::adjustment ||
                    (l.kind == LayerKind::group && !l.isolated) ||
                    (i + 1 < int(all.size()) && all[i + 1].clipped) ||
                    (previous >= 0 && previous + 1 != i))
                    throw EditError(tr("这些图层依赖其他层，请选完整组或所有可见层"));
                previous = i;
                l.visible = true;
                if (unmask)
                    l.mask_enabled = false;
                s.layers.push_back(l);
            }
            if (primary)
                sources.push_back(l.id);
        }
        if (mode != 2 && s.layers.size() != ids.size())
            throw EditError(tr("请选择顶层或完整组"));
        if (s.layers.empty() || (primary && sources.empty()))
            throw EditError(tr("输入图像为空"));
        return display_image(editing::render(s, RenderOptions{.cancelled = cancelled}),
                             QColorSpace::SRgb)
            .convertToFormat(QImage::Format_RGBA8888);
    }
    void start(bool previewOnly = false) {
        if (!canvas || busy)
            return;
        if (!QSettings().value("extensions/comfy", true).toBool())
            throw EditError(tr("ComfyUI 工作流已在首选项中停用"));
        if (!isCurrent())
            throw EditError(tr("请先切回此工作流所属的文档"));
        canvas->commit_transform();
        auto& s = canvas->project().state();
        if (qint64(s.size.width()) * s.size.height() > 32000000)
            throw EditError(tr("工作流输入当前上限为 3200 万像素"));
        for (auto* b : settings.findChildren<QComboBox*>())
            if (b->property("missing").toBool() && b->currentIndex() == 0)
                throw EditError(tr("工作流依赖缺失：") + b->currentText() +
                                tr("；请在当前服务安装或选择可用项"));
        for (auto* b : settings.findChildren<QLineEdit*>())
            if (b->property("invalid").toBool())
                throw EditError(tr("存在无效数值，请修正参数"));
        uploads.clear();
        images.clear();
        candidates.clear();
        sources.clear();
        inputSnapshots.clear();
        sourcePlan = {};
        mask = {};
        job.clear();
        nodeProgress.clear();
        submitted = graph;
        frozenGraph = graph;
        frozenManifest = manifest;
        revision = canvas->project().revision();
        downloads.clear();
        originalSize = s.size;
        for (int i = 0; i < int(s.layers.size()); ++i) {
            const auto& l = s.layers[i];
            sourcePlan.append(QJsonObject{
                {"id", l.id.toString()}, {"visible", l.visible}, {"index", i}, {"name", l.name}});
        }
        outputBounds = QRect(QPoint(), s.size);
        if (expand) {
            outputBounds.adjust(-margins[0]->value(), -margins[1]->value(), margins[2]->value(),
                                margins[3]->value());
            if (qint64(outputBounds.width()) * outputBounds.height() > 32000000)
                throw EditError(tr("扩图超过像素预算"));
        }
        int align = manifest["geometry"].toObject()["align"].toInt(1);
        if (align < 1 || align > 64)
            throw EditError(tr("无效的工作流对齐单位"));
        serviceSize = QSize((outputBounds.width() + align - 1) / align * align,
                            (outputBounds.height() + align - 1) / align * align);
        struct Input {
            QString node, key, role, kind, white;
            int mode;
        };
        QList<Input> inputs;
        for (const auto& v : slots_)
            inputs.append({v.node, v.key, v.role, v.kind, v.white, v.source->currentIndex()});
        struct Prepared {
            std::vector<QUuid> sources;
            QImage base, mask;
            QImage linearBase;
            QMap<QString, QImage> snapshots;
            QList<QPair<QString, QByteArray>> uploads;
            QString error;
        };
        auto serial = ++ticket;
        finished(true);
        elapsed.start();
        status.setText(tr("准备图层与蒙版…"));
        cancelled = std::make_shared<std::atomic_bool>(false);
        auto* watcher = new QFutureWatcher<Prepared>(this);
        connect(watcher, &QFutureWatcher<Prepared>::finished, this,
                [this, watcher, serial, previewOnly] {
                    watcher->deleteLater();
                    if (serial != ticket)
                        return;
                    guarded([&] {
                        auto data = watcher->result();
                        if (!data.error.isEmpty())
                            throw EditError(data.error);
                        sources = std::move(data.sources);
                        mask = std::move(data.mask);
                        base = std::move(data.base);
                        linearBase = std::move(data.linearBase);
                        inputSnapshots = std::move(data.snapshots);
                        inputSnapshots["primarySnapshot"] = base;
                        if (!mask.isNull())
                            inputSnapshots["editMaskSnapshot"] = mask;
                        uploads = std::move(data.uploads);
                        if (previewOnly) {
                            thumbnail.setPixmap(QPixmap::fromImage(base.scaled(
                                260, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                            thumbnail.show();
                            QStringList names;
                            for (auto id : sources)
                                if (auto* l = find_layer(canvas->project().state().layers, id))
                                    names << l->name;
                            finished(false);
                            status.setText(tr("引用图层：%1").arg(names.join("、")));
                            return;
                        }
                        upload(0);
                    });
                });
        watcher->setFuture(
            QtConcurrent::run([s, inputs, flag = cancelled, active = canvas->active_layer,
                               selected = canvas->selected_layers, bounds = outputBounds,
                               serviceSize = this->serviceSize, submitted = this->submitted,
                               previewOnly, matte = this->matte] {
                Prepared result;
                auto& base = result.base;
                auto& uploads = result.uploads;
                try {
                    for (const auto& slot : inputs) {
                        if (flag->load())
                            return result;
                        QImage im;
                        if (slot.kind == "image") {
                            im = inputImage(s, slot.mode, slot.role == "primary", active, selected,
                                            result.sources, flag.get(),
                                            matte && slot.role == "primary");
                            if (slot.role == "primary") {
                                im = padded(im, false, bounds);
                                base = im;
                            }
                        } else {
                            QImage mask;
                            int mode = slot.mode;
                            if (mode == 0) {
                                if (s.selection.isNull())
                                    throw EditError(tr("当前没有选区，请建立选区或选择全图"));
                                mask = s.selection;
                            }
                            if (mode == 1) {
                                mask = QImage(s.size, QImage::Format_Grayscale8);
                                mask.fill(255);
                            }
                            if (mode == 2) {
                                auto* l = find_layer(s.layers, active);
                                if (!l || l->mask.isNull() || s.size != l->mask.size() ||
                                    !l->mask_transform.isIdentity() || l->warped ||
                                    !parent_matrix(s, l->id).isIdentity())
                                    throw EditError(tr("请将该蒙版载入选区后提交"));
                                mask = l->mask;
                            }
                            if (mode == 3) {
                                mask = QImage(s.size, QImage::Format_Grayscale8);
                                mask.fill(0);
                            }
                            mask = padded(mask, true, bounds);
                            bool any = false;
                            for (int y = 0; y < mask.height() && !any; ++y)
                                any = std::any_of(mask.constScanLine(y),
                                                  mask.constScanLine(y) + mask.width(),
                                                  [](uchar v) { return v != 0; });
                            if (!any)
                                throw EditError(tr("蒙版全黑，没有修改范围"));
                            im = mask;
                            if (slot.role == "editMask" ||
                                std::count_if(inputs.begin(), inputs.end(),
                                              [](const auto& v) { return v.kind == "mask"; }) == 1)
                                result.mask = mask;
                            if (slot.white == "preserve")
                                im.invertPixels();
                            auto node = submitted[slot.node].toObject();
                            bool alpha =
                                node["class_type"] == "LoadImageMask" &&
                                node["inputs"].toObject()["channel"].toString("alpha") == "alpha";
                            if (node["class_type"] == "LoadImage")
                                for (auto peer : submitted.keys()) {
                                    auto ins = submitted[peer].toObject()["inputs"].toObject();
                                    for (auto v : ins) {
                                        auto a = v.toArray();
                                        if (a.size() == 2 && a[0] == slot.node && a[1].toInt() == 1)
                                            alpha = true;
                                    }
                                }
                            if (alpha) {
                                auto gray = im;
                                im = QImage(gray.size(), QImage::Format_RGBA8888);
                                im.fill(Qt::white);
                                for (int y = 0; y < im.height(); ++y) {
                                    auto* row = im.scanLine(y);
                                    auto* source = gray.constScanLine(y);
                                    for (int x = 0; x < im.width(); ++x)
                                        row[x * 4 + 3] = 255 - source[x];
                                }
                            }
                        }
                        if ((slot.role == "primary" || slot.kind == "mask") &&
                            im.size() != serviceSize)
                            im = im.copy(QRect(QPoint(), serviceSize));
                        result.snapshots[slot.node + "/" + slot.key] = im;
                        qint64 snapshotBytes = 0;
                        for (const auto& snapshot : result.snapshots)
                            snapshotBytes += snapshot.sizeInBytes();
                        if (snapshotBytes > 512LL * 1024 * 1024)
                            throw EditError(tr("输入快照超过 512 MiB，请减少参考输入或图像尺寸"));
                        if (previewOnly)
                            continue;
                        QByteArray png;
                        QBuffer buf(&png);
                        buf.open(QIODevice::WriteOnly);
                        if (!im.save(&buf, "PNG") || png.size() > 128 * 1024 * 1024)
                            throw EditError(tr("输入编码失败或超过 128 MiB"));
                        uploads.append({QUuid::createUuid().toString(QUuid::Id128) + ".png", png});
                    }
                    if (!flag->load())
                        result.linearBase = to_working(base);
                } catch (const std::exception& e) {
                    result.error = QString::fromUtf8(e.what());
                }
                return result;
            }));
    }
    void upload(int i) {
        if (i == uploads.size()) {
            auto ws = url("ws");
            ws.setScheme(ws.scheme() == "https" ? "wss" : "ws");
            QUrlQuery query;
            query.addQueryItem("clientId", client);
            ws.setQuery(query);
            progressSocket.open(req(ws));
            status.setText(tr("提交工作流…"));
            auto r = req(url("prompt"));
            r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            receive(network.post(
                        r, QJsonDocument(QJsonObject{{"prompt", submitted}, {"client_id", client}})
                               .toJson()),
                    [this](auto b) {
                        job = jsonObject(b)["prompt_id"].toString();
                        jobAddress = address.text();
                        if (job.isEmpty())
                            throw EditError(tr("服务没有返回任务 ID"));
                        poll();
                    });
            return;
        }
        status.setText(tr("上传输入 %1/%2…").arg(i + 1).arg(uploads.size()));
        auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    "form-data; name=\"image\"; filename=\"" + uploads[i].first + "\"");
        p.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        p.setBody(uploads[i].second);
        multi->append(p);
        auto* reply = network.post(req(url("upload/image")), multi);
        multi->setParent(reply);
        receive(reply, [this, i](auto b) {
            auto v = jsonObject(b);
            auto name = v["name"].toString();
            if (name.isEmpty())
                throw EditError(tr("服务未返回上传文件名"));
            auto folder = v["subfolder"].toString();
            if (!folder.isEmpty())
                name = folder + "/" + name;
            put(submitted, slots_[i].node, slots_[i].key, name);
            uploads[i].second.clear();
            upload(i + 1);
        });
    }
    void poll() {
        if (elapsed.elapsed() > 3600000)
            throw EditError(tr("等待任务超时；未改动图层"));
        status.setText(nodeProgress.isEmpty() ? tr("等待服务执行：%1").arg(job) : nodeProgress);
        receive(network.get(req(url("history/" + job))), [this](auto b) {
            auto result = jsonObject(b)[job].toObject();
            if (result.isEmpty()) {
                auto serial = ticket;
                QTimer::singleShot(1200, this, [this, serial] {
                    if (serial == ticket)
                        guarded([this] { poll(); });
                });
                return;
            }
            if (result["status"].toObject()["status_str"] == "error")
                throw EditError(tr("工作流执行失败：") +
                                QString::fromUtf8(
                                    QJsonDocument(result["status"].toObject()["messages"].toArray())
                                        .toJson(QJsonDocument::Compact))
                                    .left(900));
            for (auto binding : manifest["outputs"].toArray()) {
                auto o = binding.toObject();
                if (o["role"] != "final" || o["kind"] != "image")
                    continue;
                for (auto item : result["outputs"]
                                     .toObject()[o["node"].toString()]
                                     .toObject()["images"]
                                     .toArray())
                    downloads.append(item.toObject());
            }
            if (downloads.empty() || downloads.size() > 16)
                throw EditError(tr("最终节点未返回图像或候选数超过 16"));
            fetch(0);
        });
    }
    void fetch(int i) {
        if (i == downloads.size()) {
            finished(false);
            status.setText(tr("结果已就绪"));
            if (canvas && isCurrent() && canvas->project().revision() == revision)
                applyImage(0);
            else
                status.setText(tr("输入已变化；结果保留为候选，可另加为新图层"));
            return;
        }
        auto im = downloads[i];
        auto u = url("view");
        QUrlQuery query;
        for (auto k : {"filename", "subfolder", "type"})
            query.addQueryItem(k, im[k].toString());
        u.setQuery(query);
        status.setText(tr("下载最终图像 %1/%2").arg(i + 1).arg(downloads.size()));
        receive(
            network.get(req(u)),
            [this, i](QByteArray bytes) {
                if (qint64(images.size() + 1) * linearBase.sizeInBytes() > 1024LL * 1024 * 1024)
                    throw EditError(tr("结果候选超过 1 GiB 内存预算"));
                status.setText(tr("校验与合成结果 %1…").arg(i + 1));
                using Decoded = QPair<QImage, QString>;
                auto* watcher = new QFutureWatcher<Decoded>(this);
                auto serial = ticket;
                connect(watcher, &QFutureWatcher<Decoded>::finished, this,
                        [this, watcher, serial, i] {
                            watcher->deleteLater();
                            if (serial != ticket)
                                return;
                            guarded([&] {
                                auto result = watcher->result();
                                if (!result.second.isEmpty())
                                    throw EditError(result.second);
                                images.append(result.first);
                                candidates.addItem(tr("结果 %1").arg(i + 1));
                                fetch(i + 1);
                            });
                        });
                watcher->setFuture(QtConcurrent::run(
                    [bytes = std::move(bytes), expected = serviceSize, original = linearBase,
                     mask = mask, matte = matte, flag = cancelled]() mutable -> Decoded {
                        try {
                            QBuffer buffer(&bytes);
                            buffer.open(QIODevice::ReadOnly);
                            QImageReader reader(&buffer);
                            if (reader.size() != expected)
                                throw EditError(tr("结果尺寸与输入不一致，未替换原图层"));
                            auto img = reader.read();
                            if (img.isNull())
                                throw EditError(reader.errorString());
                            if (flag->load())
                                return {};
                            if (img.size() != original.size())
                                img = img.copy(QRect(QPoint(), original.size()));
                            if (matte)
                                return {img.convertToFormat(QImage::Format_Grayscale8), {}};
                            if (!img.colorSpace().isValid())
                                img.setColorSpace(QColorSpace::SRgb);
                            img = to_working(img);
                            if (!mask.isNull())
                                for (int y = 0; y < img.height(); ++y) {
                                    if (flag->load())
                                        return {};
                                    auto* dst = reinterpret_cast<float*>(img.scanLine(y));
                                    auto* src =
                                        reinterpret_cast<const float*>(original.constScanLine(y));
                                    auto* m = mask.constScanLine(y);
                                    for (int x = 0; x < img.width(); ++x) {
                                        float alpha = m[x] / 255.f;
                                        for (int c = 0; c < 4; ++c)
                                            dst[x * 4 + c] = dst[x * 4 + c] * alpha +
                                                             src[x * 4 + c] * (1 - alpha);
                                    }
                                }
                            return {img, {}};
                        } catch (const std::exception& e) {
                            return {{}, QString::fromUtf8(e.what())};
                        }
                    }));
            },
            128LL * 1024 * 1024);
    }
    QUuid resultLayer;
    void applyImage(int idx) {
        if (!canvas || idx < 0 || idx >= images.size())
            return;
        bool same = canvas->project().revision() == revision;
        Layer l;
        l.name = tr("生成 · ") + frozenManifest["name"].toString();
        l.pixels = matte ? linearBase : images[idx];
        if (matte)
            l.mask = images[idx];
        l.generation_inputs = inputSnapshots;
        l.source_metadata = {
            {"comfyWorkflow", frozenGraph},
            {"comfyManifest", frozenManifest},
            {"seedParameters", submitted},
            {"comfyTask",
             QJsonObject{{"promptId", job},
                         {"revision", QString::number(revision)},
                         {"sources", sourcePlan},
                         {"bounds", QJsonArray{outputBounds.x(), outputBounds.y(),
                                               outputBounds.width(), outputBounds.height()}},
                         {"serviceSize", QJsonArray{serviceSize.width(), serviceSize.height()}},
                         {"colorSpace", "sRGB"},
                         {"serverVersion", "unknown"}}}};
        QJsonArray primaryIds;
        for (auto id : sources)
            primaryIds.append(id.toString());
        auto record = l.source_metadata["comfyTask"].toObject();
        record["primaryIds"] = primaryIds;
        l.source_metadata["comfyTask"] = record;
        if (canvas->project().state().size != (resultLayer.isNull() ? originalSize : base.size()))
            throw EditError(tr("画布尺寸已变化，无法按原坐标应用"));
        if (!same && QMessageBox::question(this, tr("输入已变化"),
                                           tr("将候选另加为新图层，保留当前图层可见性？")) !=
                         QMessageBox::Yes)
            return;
        canvas->edit(tr("应用生成结果"), [&](State& s) {
            if (same && !resultLayer.isNull()) {
                auto* old = find_layer(s.layers, resultLayer);
                if (!old)
                    throw EditError(tr("结果层已删除"));
                old->pixels = l.pixels;
                if (matte)
                    old->mask = l.mask;
                l.id = old->id;
                return;
            }
            if (expand && resultLayer.isNull()) {
                if (!same)
                    throw EditError(tr("输入已变化，请重新生成扩图"));
                crop_canvas(s, outputBounds, 0, false);
            }
            auto pos = s.layers.end();
            if (same) {
                for (auto it = s.layers.begin(); it != s.layers.end(); ++it)
                    if (std::find(sources.begin(), sources.end(), it->id) != sources.end()) {
                        it->visible = false;
                        pos = it + 1;
                    }
            }
            s.layers.insert(pos, l);
        });
        resultLayer = l.id;
        canvas->active_layer = l.id;
        canvas->selected_layers = {l.id};
        revision = canvas->project().revision();
        refresh();
        status.setText(tr("已创建结果层；撤销可恢复原图层"));
    }

  public:
    WorkflowDialog(CanvasView* c, QWidget* parent, std::function<void()> update,
                   std::function<bool()> current)
        : QWidget(parent), canvas(c), isCurrent(std::move(current)), refresh(std::move(update)) {
        progressSocket.setMaxAllowedIncomingMessageSize(1024 * 1024);
        progressSocket.setMaxAllowedIncomingFrameSize(1024 * 1024);
        connect(
            &progressSocket, &QWebSocket::textMessageReceived, this, [this](const QString& text) {
                if (!busy || job.isEmpty())
                    return;
                auto message = QJsonDocument::fromJson(text.toUtf8()).object();
                auto data = message["data"].toObject();
                if (data["prompt_id"].toString() != job)
                    return;
                const auto type = message["type"].toString();
                if (type == "executing" && data["node"].isString())
                    nodeProgress = tr("正在执行节点 %1").arg(data["node"].toString());
                if (type == "progress" && data["max"].toDouble() > 0)
                    nodeProgress = tr("节点 %1 · %2 / %3")
                                       .arg(data["node"].toVariant().toString())
                                       .arg(data["value"].toDouble())
                                       .arg(data["max"].toDouble());
                if (type == "execution_error" || type == "execution_interrupted") {
                    error(tr("服务端任务中止：%1").arg(data["exception_message"].toString(type)));
                    return;
                }
                if (!nodeProgress.isEmpty())
                    status.setText(nodeProgress);
            });
        connect(&progressSocket, &QWebSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    if (busy)
                        nodeProgress = tr("实时进度连接不可用，继续查询任务结果");
                });
        setWindowTitle(tr("生成工作流 · ComfyUI"));
        setMinimumWidth(260);
        auto* layout = new QVBoxLayout(this);
        auto* form = new QFormLayout;
        address.setText(QSettings().value("comfy/address", "http://127.0.0.1:8188").toString());
        token.setEchoMode(QLineEdit::Password);
        form->addRow(tr("服务地址"), &address);
        form->addRow(tr("访问令牌（可选）"), &token);
        layout->addLayout(form);
        templates.setObjectName("comfyTemplates");
        load.setObjectName("comfyImport");
        address.setObjectName("comfyAddress");
        cancel.setObjectName("comfyCancel");
        check.setObjectName("comfyCheck");
        generate.setObjectName("comfyGenerate");
        status.setObjectName("comfyStatus");
        templates.addItems({tr("选择工作流"), tr("局部重绘 / 生成填充"), tr("生成式扩图"),
                            tr("提示词编辑（整图）"), tr("AI 蒙版边缘精修")});
        for (const auto& file : library.entryInfoList({"*.json"}, QDir::Files, QDir::Name))
            templates.addItem(file.completeBaseName(), file.absoluteFilePath());
        layout->addWidget(&templates);
        connect(&templates, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index == 0) {
                if (!workflowKey.isEmpty())
                    parameterCache[workflowKey] = graph;
                workflowKey.clear();
                job.clear();
                images.clear();
                candidates.clear();
                manifest = {};
                graph = {};
                info = {};
                rebuild();
                status.setText(tr("请选择或导入工作流"));
                return;
            }
            guarded([&] {
                if (!workflowKey.isEmpty())
                    parameterCache[workflowKey] = graph;
                workflowKey = templates.itemData(index).toString();
                if (workflowKey.isEmpty())
                    workflowKey = QString::number(index);
                if (!templates.itemData(index).toString().isEmpty()) {
                    auto bundle = readJson(workflowKey);
                    manifest = bundle["manifest"].toObject();
                    graph = bundle["graph"].toObject();
                    templates.setItemText(index, manifest["name"].toString(workflowKey));
                } else {
                    manifest = readJson(":/huelith/workflows/manifest.json");
                    graph = readJson(":/huelith/workflows/inpaint.json");
                    if (index == 3) {
                        manifest["name"] = tr("提示词编辑");
                        put(graph, "7", "denoise", 0.55);
                        graph["6"] =
                            QJsonObject{{"class_type", "VAEEncode"},
                                        {"inputs", QJsonObject{{"pixels", QJsonArray{"2", 0}},
                                                               {"vae", QJsonArray{"1", 2}}}}};
                        graph.remove("3");
                        manifest["inputs"] = QJsonArray{manifest["inputs"].toArray().at(0)};
                    }
                    if (index == 4) {
                        manifest["name"] = tr("蒙版边缘精修");
                        manifest["id"] = "huelith.matte";
                        manifest["resultMode"] = "layerMask";
                        manifest["geometry"] = QJsonObject{{"mode", "sameAsInput"}};
                        manifest["parameters"] =
                            QJsonArray{QJsonObject{{"node", "4"},
                                                   {"input", "kernel_size"},
                                                   {"label", tr("边缘搜索范围")}}};
                        graph = readJson(":/huelith/workflows/matting.json");
                    }
                    if (index == 2) {
                        manifest["name"] = tr("扩图");
                        auto geom = manifest["geometry"].toObject();
                        geom["mode"] = "outpaint";
                        manifest["geometry"] = geom;
                    }
                }
                if (parameterCache.contains(workflowKey))
                    graph = parameterCache[workflowKey];
                job.clear();
                images.clear();
                candidates.clear();
                info = {};
                resultLayer = {};
                rebuild();
            });
        });
        auto* lib = new QHBoxLayout;
        lib->addWidget(&load);
        lib->addWidget(&check);
        layout->addLayout(lib);
        search.setPlaceholderText(tr("搜索节点或参数"));
        layout->addWidget(&search);
        scroll.setWidgetResizable(true);
        scroll.setWidget(&settings);
        layout->addWidget(&scroll, 1);
        status.setWordWrap(true);
        layout->addWidget(&status);
        thumbnail.hide();
        layout->addWidget(&thumbnail);
        layout->addWidget(&candidates);
        auto* aux = new QHBoxLayout;
        aux->addWidget(&preview);
        aux->addWidget(&retry);
        layout->addLayout(aux);
        connect(&preview, &QPushButton::clicked, this,
                [this] { guarded([this] { start(true); }); });
        connect(&retry, &QPushButton::clicked, this, [this] {
            guarded([this] {
                downloads.clear();
                images.clear();
                candidates.clear();
                ++ticket;
                elapsed.restart();
                finished(true);
                poll();
            });
        });
        auto* actions = new QHBoxLayout;
        actions->addWidget(&generate);
        actions->addWidget(&cancel);
        actions->addWidget(&apply);
        layout->addLayout(actions);
        connect(&search, &QLineEdit::textChanged, this, [this](auto word) {
            for (auto* b : settings.findChildren<QGroupBox*>())
                b->setVisible(b->property("words").toString().contains(word, Qt::CaseInsensitive));
        });
        connect(&address, &QLineEdit::textChanged, this, [this] {
            ++ticket;
            info = {};
            finished(false);
            status.setText(tr("服务地址已改变，请重新检查"));
        });
        connect(&load, &QPushButton::clicked, this, [this] {
            guarded([&] {
                QString path = QFileDialog::getOpenFileName(
                    this, tr("选择工作流描述文件"),
                    QSettings().value("comfy/lastManifest").toString(), "Manifest (manifest.json)");
                if (path.isEmpty())
                    return;
                auto m = readJson(path);
                QString name = m["workflow"].toString("workflow_api.json");
                QFileInfo f(QFileInfo(path).dir(), name);
                if (m["schemaVersion"].toInt() != 1 || QFileInfo(name).fileName() != name ||
                    QFileInfo(f.canonicalFilePath()).absolutePath() !=
                        QFileInfo(path).canonicalPath())
                    throw EditError(tr("描述版本或工作流相对路径无效"));
                auto g = readJson(f.absoluteFilePath());
                if (!library.mkpath("."))
                    throw EditError(tr("无法建立工作流库"));
                auto data = QJsonDocument(QJsonObject{{"manifest", m}, {"graph", g}})
                                .toJson(QJsonDocument::Compact);
                if (data.size() > 4 * 1024 * 1024)
                    throw EditError(tr("工作流包超过 4 MiB"));
                QString id = QString::fromLatin1(
                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
                QString stored = library.filePath(id + ".json");
                QSaveFile fcopy(stored);
                if (!fcopy.open(QIODevice::WriteOnly) || fcopy.write(data) != data.size() ||
                    !fcopy.commit())
                    throw EditError(fcopy.errorString());
                int index = templates.findData(stored);
                if (index < 0) {
                    templates.addItem(m["name"].toString(id.left(8)), stored);
                    index = templates.count() - 1;
                }
                QSettings().setValue("comfy/lastManifest", path);
                templates.setCurrentIndex(index);
            });
        });
        connect(&check, &QPushButton::clicked, this, [this] {
            guarded([&] {
                if (graph.isEmpty())
                    throw EditError(tr("请先导入工作流"));
                auto u = url("object_info");
                ++ticket;
                info = {};
                finished(true);
                status.setText(tr("检查节点与模型…"));
                receive(
                    network.get(req(u)),
                    [this](auto b) {
                        info = jsonObject(b);
                        QSettings().setValue("comfy/address", address.text());
                        rebuild();
                    },
                    64LL * 1024 * 1024);
            });
        });
        connect(&generate, &QPushButton::clicked, this, [this] {
            guarded([&] {
                resultLayer = {};
                start();
            });
        });
        connect(&cancel, &QPushButton::clicked, this, [this] {
            ++ticket;
            if (cancelled)
                cancelled->store(true);
            for (auto* r : network.findChildren<QNetworkReply*>())
                if (r->isRunning())
                    r->abort();
            uploads.clear();
            images.clear();
            candidates.clear();
            finished(false);
            if (!job.isEmpty()) {
                auto r = req(url("queue"));
                r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                auto* reply = network.post(
                    r, QJsonDocument(QJsonObject{{"delete", QJsonArray{job}}}).toJson());
                connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
            }
            job.clear();
            retry.setEnabled(false);
            status.setText(tr("已停止应用结果；已运行的服务端任务可能继续"));
        });
        connect(&apply, &QPushButton::clicked, this,
                [this] { guarded([&] { applyImage(candidates.currentIndex()); }); });
        if (const auto* layer = find_layer(c->project().state().layers, c->active_layer);
            layer && layer->source_metadata.contains("comfyWorkflow")) {
            manifest = layer->source_metadata["comfyManifest"].toObject();
            graph = layer->source_metadata["comfyWorkflow"].toObject();
            if (!graph.isEmpty() && !manifest.isEmpty()) {
                rebuild();
                auto original = layer->generation_inputs.value("primarySnapshot");
                if (!original.isNull()) {
                    thumbnail.setPixmap(QPixmap::fromImage(
                        original.scaled(260, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    thumbnail.show();
                    thumbnail.setToolTip(tr("此结果生成时的输入快照，已随工程保存"));
                }
                status.setText(tr("已恢复结果层的工作流参数；可离线查看，生成前请检查服务"));
            }
        }
        finished(false);
    }
    ~WorkflowDialog() override {
        progressSocket.abort();
        if (cancelled)
            cancelled->store(true);
        scroll.takeWidget();
        ++ticket;
        for (auto* r : network.findChildren<QNetworkReply*>())
            r->disconnect(this);
    }
};
} // namespace
void EditorController::show_comfy() {
    if (!canvas())
        return;
    if (!QSettings().value("extensions/comfy", true).toBool())
        throw EditError(tr("请先在首选项中启用 ComfyUI 工作流"));
    auto* target = canvas();
    for (auto* dock : window_->findChildren<QDockWidget*>()) {
        if (dock->property("workflowCanvas").value<QObject*>() == target) {
            dock->show();
            dock->raise();
            return;
        }
    }
    auto* dock =
        new QDockWidget(tr("生成工作流 · %1").arg(target->project().state().name), window_);
    dock->setProperty("workflowCanvas", QVariant::fromValue<QObject*>(target));
    dock->setObjectName("comfyDock_" + QUuid::createUuid().toString(QUuid::Id128));
    dock->setAttribute(Qt::WA_DeleteOnClose);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* pane = new WorkflowDialog(
        target, dock, [this] { refresh(); }, [this, target] { return canvas() == target; });
    pane->setObjectName("comfyWorkflow");
    dock->setWidget(pane);
    connect(target, &QObject::destroyed, dock, &QObject::deleteLater);
    connect(window_->documents_, &QTabWidget::currentChanged, dock,
            [this, target, dock] { dock->setVisible(canvas() == target); });
    window_->addDockWidget(Qt::RightDockWidgetArea, dock);
    if (auto* props = window_->findChild<QDockWidget*>("propertiesDock"))
        window_->tabifyDockWidget(props, dock);
    dock->show();
    dock->raise();
}
} // namespace huelith::desktop
