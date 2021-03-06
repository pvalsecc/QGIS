/***************************************************************************
    qgslinuxnative.h
                             -------------------
    begin                : July 2018
    copyright            : (C) 2018 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgslinuxnative.h"

#include <QCoreApplication>
#include <QUrl>
#include <QString>
#include <QtDBus/QtDBus>
#include <QtDebug>
#include <QImage>

QgsNative::Capabilities QgsLinuxNative::capabilities() const
{
  return NativeDesktopNotifications;
}

void QgsLinuxNative::initializeMainWindow( QWindow *,
    const QString &,
    const QString &,
    const QString & )
{
  // Hardcoded desktop file value matching our official .deb packages
  mDesktopFile = QStringLiteral( "qgis.desktop" );
}

void QgsLinuxNative::openFileExplorerAndSelectFile( const QString &path )
{
  if ( !QDBusConnection::sessionBus().isConnected() )
  {
    QgsNative::openFileExplorerAndSelectFile( path );
    return;
  }

  QDBusInterface iface( QStringLiteral( "org.freedesktop.FileManager1" ),
                        QStringLiteral( "/org/freedesktop/FileManager1" ),
                        QStringLiteral( "org.freedesktop.FileManager1" ),
                        QDBusConnection::sessionBus() );

  iface.call( QDBus::NoBlock, QStringLiteral( "ShowItems" ), QStringList( QUrl::fromLocalFile( path ).toString() ), QStringLiteral( "QGIS" ) );
  if ( iface.lastError().type() != QDBusError::NoError )
  {
    QgsNative::openFileExplorerAndSelectFile( path );
  }
}

void QgsLinuxNative::showUndefinedApplicationProgress()
{
  const QVariantMap properties
  {
    { QStringLiteral( "progress-visible" ), true },
    { QStringLiteral( "progress" ), 0.0 }
  };

  QDBusMessage message = QDBusMessage::createSignal( QStringLiteral( "/org/qgis/UnityLauncher" ),
                         QStringLiteral( "com.canonical.Unity.LauncherEntry" ),
                         QStringLiteral( "Update" ) );
  message.setArguments( {mDesktopFile, properties} );
  QDBusConnection::sessionBus().send( message );
}

void QgsLinuxNative::setApplicationProgress( double progress )
{
  const QVariantMap properties
  {
    { QStringLiteral( "progress-visible" ), true },
    { QStringLiteral( "progress" ), progress / 100.0 }
  };

  QDBusMessage message = QDBusMessage::createSignal( QStringLiteral( "/org/qgis/UnityLauncher" ),
                         QStringLiteral( "com.canonical.Unity.LauncherEntry" ),
                         QStringLiteral( "Update" ) );
  message.setArguments( {mDesktopFile, properties} );
  QDBusConnection::sessionBus().send( message );
}

void QgsLinuxNative::hideApplicationProgress()
{
  const QVariantMap properties
  {
    { QStringLiteral( "progress-visible" ), false },
  };

  QDBusMessage message = QDBusMessage::createSignal( QStringLiteral( "/org/qgis/UnityLauncher" ),
                         QStringLiteral( "com.canonical.Unity.LauncherEntry" ),
                         QStringLiteral( "Update" ) );
  message.setArguments( {mDesktopFile, properties} );
  QDBusConnection::sessionBus().send( message );
}

/**
 * Automatic marshaling of a QImage for org.freedesktop.Notifications.Notify
 *
 * This function is from the Clementine project (see
 * http://www.clementine-player.org) and licensed under the GNU General Public
 * License, version 3 or later.
 *
 * Copyright 2010, David Sansome <me@davidsansome.com>
 */
QDBusArgument &operator<<( QDBusArgument &arg, const QImage &image )
{
  if ( image.isNull() )
  {
    arg.beginStructure();
    arg << 0 << 0 << 0 << false << 0 << 0 << QByteArray();
    arg.endStructure();
    return arg;
  }

  QImage scaled = image.scaledToHeight( 100, Qt::SmoothTransformation );
  scaled = scaled.convertToFormat( QImage::Format_ARGB32 );

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  // ABGR -> ARGB
  QImage i = scaled.rgbSwapped();
#else
  // ABGR -> GBAR
  QImage i( scaled.size(), scaled.format() );
  for ( int y = 0; y < i.height(); ++y )
  {
    QRgb *p = ( QRgb * ) scaled.scanLine( y );
    QRgb *q = ( QRgb * ) i.scanLine( y );
    QRgb *end = p + scaled.width();
    while ( p < end )
    {
      *q = qRgba( qGreen( *p ), qBlue( *p ), qAlpha( *p ), qRed( *p ) );
      p++;
      q++;
    }
  }
#endif

  arg.beginStructure();
  arg << i.width();
  arg << i.height();
  arg << i.bytesPerLine();
  arg << i.hasAlphaChannel();
  int channels = i.isGrayscale() ? 1 : ( i.hasAlphaChannel() ? 4 : 3 );
  arg << i.depth() / channels;
  arg << channels;
  arg << QByteArray( reinterpret_cast<const char *>( i.bits() ), i.numBytes() );
  arg.endStructure();
  return arg;
}

const QDBusArgument &operator>>( const QDBusArgument &arg, QImage & )
{
  // This is needed to link but shouldn't be called.
  Q_ASSERT( 0 );
  return arg;
}

QgsNative::NotificationResult QgsLinuxNative::showDesktopNotification( const QString &summary, const QString &body, const NotificationSettings &settings )
{
  NotificationResult result;
  result.successful = false;

  if ( !QDBusConnection::sessionBus().isConnected() )
  {
    return result;
  }

  qDBusRegisterMetaType<QImage>();

  QDBusInterface iface( QStringLiteral( "org.freedesktop.Notifications" ),
                        QStringLiteral( "/org/freedesktop/Notifications" ),
                        QStringLiteral( "org.freedesktop.Notifications" ),
                        QDBusConnection::sessionBus() );

  QVariantMap hints;
  hints[QStringLiteral( "transient" )] = settings.transient;
  if ( !settings.image.isNull() )
    hints[QStringLiteral( "image_data" )] = settings.image;

  QVariantList argumentList;
  argumentList << "qgis"; //app_name
  // replace_id
  if ( settings.messageId.isValid() )
    argumentList << static_cast< uint >( settings.messageId.toInt() );
  else
    argumentList << static_cast< uint >( 0 );
  // app_icon
  if ( !settings.svgAppIconPath.isEmpty() )
    argumentList << settings.svgAppIconPath;
  else
    argumentList << "";
  argumentList << summary; // summary
  argumentList << body; // body
  argumentList << QStringList();  // actions
  argumentList << hints;  // hints
  argumentList << -1; // timeout in ms "If -1, the notification's expiration time is dependent on the notification server's settings, and may vary for the type of notification."

  QDBusMessage reply = iface.callWithArgumentList( QDBus::AutoDetect, QStringLiteral( "Notify" ), argumentList );
  if ( reply.type() == QDBusMessage::ErrorMessage )
  {
    qDebug() << "D-Bus Error:" << reply.errorMessage();
    return result;
  }

  result.successful = true;
  result.messageId = reply.arguments().value( 0 );
  return result;
}
