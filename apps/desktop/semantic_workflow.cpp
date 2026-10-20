#include "canvas_view.h"
#include "editor_controller.h"
#include "main_window.h"
#include "semantic_dialog.h"
#include <QPointer>
namespace huelith::desktop {
void EditorController::semantic_selection(int target) {
    auto* c = canvas();
    if (!c)
        return;
    const QPointer<CanvasView> guard(c);
    const auto revision = c->project().revision();
    const auto size = c->project().state().size;
    const auto proxy = editing::preview_state(c->project().state());
    SemanticDialog dialog(editing::render(proxy, {double(proxy.size.width()) / size.width()}),
                          editing::SemanticTarget(target), window_, target != 3 && target != 6);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!guard || guard->project().revision() != revision)
        throw editing::EditError(tr("图像已更改，请重新识别范围"));
    const auto mask = dialog.mask().scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    c->edit(tr("选择范围"), [&](editing::State& state) { state.selection = mask; });
}
} // namespace huelith::desktop
