#include "huelith/validation/state.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <stdexcept>

namespace huelith::validation {
TEST(M0State, CancelNestedSessionAndCommitAsOneUndo) {
    Document doc({"one", "original", 0.0});
    {
        auto session = doc.begin_session();
        session.draft.block = "nested liquify";
    }
    EXPECT_EQ(doc.snapshot().state.block, "original");
    auto session = doc.begin_session();
    session.draft.block = "repair then liquify";
    session.draft.parameter = 0.5;
    ASSERT_TRUE(doc.commit(session));
    ASSERT_TRUE(doc.undo());
    EXPECT_EQ(doc.snapshot().state.block, "original");
    EXPECT_FALSE(doc.undo());
    ASSERT_TRUE(doc.redo());
    EXPECT_EQ(doc.snapshot().state.parameter, 0.5);
}
TEST(M0State, LateResultCannotOverwriteAfterUndoOrDifferentDocument) {
    Document doc({"one", "original", 0.0});
    auto late = doc.begin_session();
    late.draft.block = "late";
    auto fresh = doc.begin_session();
    fresh.draft.parameter = 1.0;
    ASSERT_TRUE(doc.commit(fresh));
    ASSERT_TRUE(doc.undo());
    EXPECT_FALSE(doc.commit(late));
    Document other({"two", "original", 0.0});
    EXPECT_FALSE(other.commit(late));
}
TEST(M0State, SavingPinnedRevisionDoesNotClearNewEdits) {
    Document doc({"one", "original", 0.0});
    auto session = doc.begin_session();
    session.draft.parameter = 1.0;
    ASSERT_TRUE(doc.commit(session));
    const auto saving = doc.snapshot();
    session = doc.begin_session();
    session.draft.parameter = 2.0;
    ASSERT_TRUE(doc.commit(session));
    doc.mark_saved(saving.revision);
    EXPECT_TRUE(doc.dirty());
    doc.mark_saved(doc.snapshot().revision);
    EXPECT_FALSE(doc.dirty());
}
TEST(M0Storage, RoundTripChinesePathAndFailedReplacementPreservesOldFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = dir.filePath(QString::fromUtf8("测试 工程.huelith-m0"));
    const Snapshot original{{"one", QByteArray("a\0b", 3), 0.25}, 1};
    ASSERT_TRUE(save_snapshot(path, original).isEmpty());
    ASSERT_TRUE(load_snapshot(path).error.isEmpty());
    EXPECT_EQ(load_snapshot(path).state, original.state);
    auto changed = original;
    changed.state.block = "replacement";
    EXPECT_FALSE(save_snapshot(path, changed, SaveFault::before_commit).isEmpty());
    EXPECT_EQ(load_snapshot(path).state, original.state);
}
TEST(M0Storage, RejectCorruptionAndFutureSchemaMigrateOldSchemaWithoutWritingSource) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto path = dir.filePath("test");
    ASSERT_TRUE(save_snapshot(path, {{"one", "data", 0.0}, 0}).isEmpty());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    auto object = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    const auto write = [&] {
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const auto bytes = QJsonDocument(object).toJson();
        const bool ok = file.write(bytes) == bytes.size();
        file.close();
        return ok;
    };
    object["schema"] = 0;
    ASSERT_TRUE(write());
    EXPECT_TRUE(load_snapshot(path).migrated);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(QJsonDocument::fromJson(file.readAll()).object()["schema"].toInt(), 0);
    file.close();
    object["schema"] = 99;
    ASSERT_TRUE(write());
    EXPECT_FALSE(load_snapshot(path).error.isEmpty());
    object["schema"] = 1;
    object["block"] = "Y29ycnVwdA==";
    ASSERT_TRUE(write());
    EXPECT_FALSE(load_snapshot(path).error.isEmpty());
}
TEST(M0State, TimeAndDeviceGenerationArePartOfCacheIdentity) {
    const FrameKey still{"one", 1, 0, 0};
    EXPECT_NE(still, (FrameKey{"one", 1, 1, 0}));
    EXPECT_NE(still, (FrameKey{"one", 1, 0, 1}));
    EXPECT_NE(still, (FrameKey{"two", 1, 0, 0}));
    FrameCache cache;
    const Snapshot snapshot{{"one", {}, 0.25}, 1};
    EXPECT_DOUBLE_EQ(cache.evaluate(snapshot, 0, 0), 0.25);
    EXPECT_DOUBLE_EQ(cache.evaluate(snapshot, 1000, 0), 1.25);
    EXPECT_DOUBLE_EQ(cache.evaluate(snapshot, 0, 0), 0.25);
    EXPECT_EQ(cache.entry_count(), 2);
    EXPECT_DOUBLE_EQ(cache.evaluate(snapshot, 0, 1), 0.25);
    EXPECT_EQ(cache.entry_count(), 3);
}
TEST(M0State, SparseBlockDeltaRoundTripRejectsWrongBase) {
    const QByteArray source(1024 * 1024, 'a');
    auto changed = source;
    changed[500] = 'b';
    changed[510] = 'c';
    const auto delta = block_delta(source, changed);
    EXPECT_EQ(delta.offset, 500);
    EXPECT_EQ(delta.after.size(), 11);
    EXPECT_EQ(apply_delta(source, delta, false), changed);
    EXPECT_EQ(apply_delta(changed, delta, true), source);
    EXPECT_THROW(static_cast<void>(apply_delta(changed, delta, false)), std::invalid_argument);
}
} // namespace huelith::validation
