#pragma once
#include <QString>
#include <QJsonObject>
#include <atomic>
namespace huelith::editing {
struct CredentialSettings { QString tool, signer, trust_anchors; };
struct CredentialReport { bool present{}, valid{}, trusted{}; QJsonObject details; };
CredentialReport inspect_credentials(const QString& path,const CredentialSettings& settings,
                                     const std::atomic_bool* cancel=nullptr);
void sign_credentials(const QString& source,const QString& output,const QString& title,
                      const CredentialSettings& settings,const std::atomic_bool* cancel=nullptr);
}
