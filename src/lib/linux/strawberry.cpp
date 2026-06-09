// Sane Break is a gentle break reminder that helps you avoid mindlessly skipping breaks
// Copyright (C) 2026 Sane Break developers
// SPDX-License-Identifier: GPL-3.0-or-later

#include "strawberry.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QStringList>
#include <QVariantMap>

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
    updateInfo({}, {}, {});
    return;
  }

  // Metadata is a{sv}. The inner QVariant holds a QDBusArgument in read mode;
  // iterate it manually as a map rather than using the bulk >> operator.
  const QDBusArgument arg = reply.value().variant().value<QDBusArgument>();
  if (arg.currentType() != QDBusArgument::MapType) {
    m_lengthMicros = 0;
    updateInfo({}, {}, {});
    return;
  }

  QString title;
  QString album;
  QStringList artists;
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
    }
  }
  arg.endMap();

  m_lengthMicros = length;
  updateInfo(title, album, artists.value(0));
}

qint64 StrawberryWatcher::positionMicroseconds() const {
  QDBusReply<QDBusVariant> reply = m_iface->call("Get", kPlayerIface, "Position");
  if (!reply.isValid()) return 0;
  return reply.value().variant().toLongLong();
}

void StrawberryWatcher::updateInfo(const QString& title, const QString& album,
                                   const QString& artist) {
  if (m_title == title && m_album == album && m_artist == artist) return;
  m_title = title;
  m_album = album;
  m_artist = artist;
  emit trackInfoChanged(m_title, m_album, m_artist);
}

void StrawberryWatcher::onPropertiesChanged(const QString& /*interface*/,
                                            const QVariantMap& changed,
                                            const QStringList& /*invalidated*/) {
  if (changed.contains("Metadata")) fetchAndNotify();
}
