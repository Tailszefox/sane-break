// Sane Break is a gentle break reminder that helps you avoid mindlessly skipping breaks
// Copyright (C) 2026 Sane Break developers
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusInterface>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <utility>

class StrawberryWatcher : public QObject {
  Q_OBJECT

 public:
  explicit StrawberryWatcher(QObject* parent = nullptr);

  QString title() const { return m_title; }
  QString album() const { return m_album; }
  QString artist() const { return m_artist; }
  // Track length, taken from the metadata, in microseconds (0 if unknown).
  qint64 lengthMicroseconds() const { return m_lengthMicros; }
  // Current playback position, queried live over D-Bus, in microseconds.
  qint64 positionMicroseconds() const;
  // 1-based position of the track in its playlist and the playlist length,
  // read from Strawberry's database (-1 if unknown or not in a playlist).
  int playlistPosition() const { return m_playlistPosition; }
  int playlistLength() const { return m_playlistLength; }

 signals:
  void trackInfoChanged(QString title, QString album, QString artist);

 private:
  QDBusInterface* m_iface;
  QString m_title;
  QString m_album;
  QString m_artist;
  qint64 m_lengthMicros = 0;
  int m_playlistPosition = -1;
  int m_playlistLength = -1;

  void fetchAndNotify();
  std::pair<int, int> fetchPlaylistInfo(const QString& trackId) const;
  void updateInfo(const QString& title, const QString& album, const QString& artist,
                  int playlistPosition, int playlistLength);

 private slots:
  void onPropertiesChanged(const QString& interface,
                           const QVariantMap& changedProperties,
                           const QStringList& invalidated);
};
