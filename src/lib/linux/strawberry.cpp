// Sane Break is a gentle break reminder that helps you avoid mindlessly skipping breaks
// Copyright (C) 2026 Sane Break developers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "strawberry.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>
#include <utility>

static constexpr auto kService = "org.mpris.MediaPlayer2.strawberry";
static constexpr auto kPath = "/org/mpris/MediaPlayer2";
static constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
static constexpr auto kPlayerIface = "org.mpris.MediaPlayer2.Player";

StrawberryWatcher::StrawberryWatcher(QObject* parent)
    : QObject(parent),
      m_iface(new QDBusInterface(kService, kPath, kPropsIface,
                                 QDBusConnection::sessionBus(), this)) {
  QDBusConnection::sessionBus().connect(
      kService, kPath, kPropsIface, "PropertiesChanged", this,
      SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
  fetchAndNotify();
}

void StrawberryWatcher::fetchAndNotify() {
  QDBusReply<QDBusVariant> reply = m_iface->call("Get", kPlayerIface, "Metadata");
  if (!reply.isValid()) {
    m_lengthMicros = 0;
    updateInfo({}, {}, {}, -1, -1);
    return;
  }

  // Metadata is a{sv}. The inner QVariant holds a QDBusArgument in read mode;
  // iterate it manually as a map rather than using the bulk >> operator.
  const QDBusArgument arg = reply.value().variant().value<QDBusArgument>();
  if (arg.currentType() != QDBusArgument::MapType) {
    m_lengthMicros = 0;
    updateInfo({}, {}, {}, -1, -1);
    return;
  }

  QString title;
  QString album;
  QStringList artists;
  QString trackId;
  qint64 length = 0;
  arg.beginMap();
  while (!arg.atEnd()) {
    QString key;
    QVariant value;
    arg.beginMapEntry();
    arg >> key >> value;
    arg.endMapEntry();

    if (key == "xesam:title") {
      title = value.toString();
    } else if (key == "xesam:album") {
      album = value.toString();
    } else if (key == "xesam:artist") {
      // xesam:artist is an array of strings (as).
      if (value.canConvert<QStringList>()) {
        artists = value.toStringList();
      } else if (value.canConvert<QDBusArgument>()) {
        value.value<QDBusArgument>() >> artists;
      }
    } else if (key == "mpris:length") {
      // mpris:length is the track length in microseconds.
      length = value.toLongLong();
    } else if (key == "mpris:trackid") {
      // mpris:trackid is a D-Bus object path (o).
      trackId = value.value<QDBusObjectPath>().path();
      if (trackId.isEmpty()) trackId = value.toString();
    }
  }
  arg.endMap();

  const auto [playlistPosition, playlistLength] = fetchPlaylistInfo(trackId);
  m_lengthMicros = length;
  updateInfo(title, album, artists.value(0), playlistPosition, playlistLength);
}

// Look up the track's 1-based position in its playlist and the playlist
// length in Strawberry's database. Returns {-1, -1} if the track is not in a
// playlist or the database can't be read.
std::pair<int, int> StrawberryWatcher::fetchPlaylistInfo(const QString& trackId) const {
  // Strawberry's MPRIS trackid ends with the track UUID. D-Bus object paths
  // can't contain hyphens, so they're stored as underscores; convert them
  // back to match the UUID stored in the database.
  QString uuid = trackId.section('/', -1);
  uuid.replace('_', '-');
  if (uuid.isEmpty()) return {-1, -1};

  std::pair<int, int> info{-1, -1};
  const QString connectionName = "strawberry-playlist";
  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    db.setDatabaseName(
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        "/strawberry/strawberry/strawberry.db");
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    if (db.open()) {
      // Locate the currently playing track by its UUID.
      QSqlQuery trackQuery(db);
      trackQuery.prepare("SELECT playlist, rowid FROM playlist_items WHERE uuid = ?");
      trackQuery.addBindValue(uuid);
      if (trackQuery.exec() && trackQuery.next()) {
        const QVariant playlist = trackQuery.value(0);
        const QVariant rowid = trackQuery.value(1);

        // Rows are stored in playlist order, so the 1-based position is the
        // number of rows up to and including this one in the same playlist.
        QSqlQuery positionQuery(db);
        positionQuery.prepare(
            "SELECT COUNT(*) FROM playlist_items WHERE playlist = ? AND rowid <= ?");
        positionQuery.addBindValue(playlist);
        positionQuery.addBindValue(rowid);

        // Total number of tracks in that playlist.
        QSqlQuery lengthQuery(db);
        lengthQuery.prepare("SELECT COUNT(*) FROM playlist_items WHERE playlist = ?");
        lengthQuery.addBindValue(playlist);

        if (positionQuery.exec() && positionQuery.next() && lengthQuery.exec() &&
            lengthQuery.next()) {
          info = {positionQuery.value(0).toInt(), lengthQuery.value(0).toInt()};
        }
      }
    }
  }
  // The QSqlDatabase and QSqlQuery objects must be destroyed (end of the
  // scope above) before the connection is removed.
  QSqlDatabase::removeDatabase(connectionName);
  return info;
}

qint64 StrawberryWatcher::positionMicroseconds() const {
  QDBusReply<QDBusVariant> reply = m_iface->call("Get", kPlayerIface, "Position");
  if (!reply.isValid()) return 0;
  return reply.value().variant().toLongLong();
}

void StrawberryWatcher::updateInfo(const QString& title, const QString& album,
                                   const QString& artist, int playlistPosition,
                                   int playlistLength) {
  if (m_title == title && m_album == album && m_artist == artist &&
      m_playlistPosition == playlistPosition && m_playlistLength == playlistLength)
    return;
  m_title = title;
  m_album = album;
  m_artist = artist;
  m_playlistPosition = playlistPosition;
  m_playlistLength = playlistLength;
  emit trackInfoChanged(m_title, m_album, m_artist);
}

void StrawberryWatcher::onPropertiesChanged(const QString& /*interface*/,
                                            const QVariantMap& changed,
                                            const QStringList& /*invalidated*/) {
  if (changed.contains("Metadata")) fetchAndNotify();
}
