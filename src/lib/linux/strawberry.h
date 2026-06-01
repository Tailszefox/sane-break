// Sane Break is a gentle break reminder that helps you avoid mindlessly skipping breaks
// Copyright (C) 2026 Sane Break developers
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusInterface>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class StrawberryWatcher : public QObject {
  Q_OBJECT

 public:
  explicit StrawberryWatcher(QObject* parent = nullptr);

  QString title() const { return m_title; }
  QString artist() const { return m_artist; }

 signals:
  void trackInfoChanged(QString title, QString artist);

 private:
  QDBusInterface* m_iface;
  QString m_title;
  QString m_artist;

  void fetchAndNotify();
  void updateInfo(const QString& title, const QString& artist);

 private slots:
  void onPropertiesChanged(const QString& interface,
                           const QVariantMap& changedProperties,
                           const QStringList& invalidated);
};
