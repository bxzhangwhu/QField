/***************************************************************************
                            qgismobileapp.cpp
                              -------------------
              begin                : Wed Apr 04 10:48:28 CET 2012
              copyright            : (C) 2012 by Marco Bernasocchi
              email                : marco@bernawebdesign.ch
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QApplication>

#include <QtQml/QQmlEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickItem>
#include <QtWidgets/QDesktopWidget>
#include <QtWidgets/QFileDialog> // Until native looking QML dialogs are implemented (Qt5.4?)
#include <QtWidgets/QMenu> // Until native looking QML dialogs are implemented (Qt5.4?)
#include <QtWidgets/QMenuBar>
#include <QStandardItemModel>
#include <QtCore/QTimer>

#include <qgsproject.h>
#include <qgsmaplayerregistry.h>
#include <qgsmaptoolidentify.h>
#include <qgsfeature.h>
#include <qgsvectorlayer.h>

#include "qgismobileapp.h"
#include "qgsquickmapcanvasmap.h"
#include "appinterface.h"
#include "featurelistmodelselection.h"
#include "featurelistmodelhighlight.h"
#include "qgseditorwidgetregistry.h"

QgisMobileapp::QgisMobileapp( QgsApplication *app, QWindow *parent )
  : QQuickView( parent )
  , mIface( new AppInterface( this ) )
{
  initDeclarative();

  QgsEditorWidgetRegistry::initEditors();

  setSource( QUrl( "qrc:/qml/qgismobileapp.qml" ) );

  connect( engine(), SIGNAL( quit() ), app, SLOT( quit() ) );

  setMinimumHeight( 480 );
  setMinimumWidth( 800 );

  QgsQuickMapCanvasMap* mapCanvasBridge = rootObject()->findChild<QgsQuickMapCanvasMap*>();

  Q_ASSERT_X( mapCanvasBridge, "QML Init", "QgsQuickMapCanvasMap not found. It is likely that we failed to load the QML files. Check debug output for related messages." );

  // Setup map canvas
  mMapCanvas = mapCanvasBridge->mapCanvas();

  mMapCanvas->setVisible( true );

  connect( this, SIGNAL( closing( QQuickCloseEvent* ) ), QgsApplication::instance(), SLOT( quit() ) );

  connect( QgsProject::instance(), SIGNAL( onReadProject( QDomDocument ) ), this, SLOT( onReadProject( QDomDocument ) ) );

  mLayerTreeCanvasBridge = new QgsLayerTreeMapCanvasBridge( QgsProject::instance()->layerTreeRoot(), mMapCanvas, this );
  connect( QgsProject::instance(), SIGNAL( writeProject( QDomDocument& ) ), mLayerTreeCanvasBridge, SLOT( writeProject( QDomDocument& ) ) );
  connect( QgsProject::instance(), SIGNAL( onReadProject( QDomDocument ) ), mLayerTreeCanvasBridge, SLOT( onReadProject( QDomDocument ) ) );
  connect( mapCanvasBridge, SIGNAL( identifyFeature( QPointF ) ), this, SLOT( identifyFeature( QPointF ) ) );
  connect( this, SIGNAL( loadProjectStarted( QString ) ), mIface, SIGNAL( loadProjectStarted( QString ) ) );
  connect( this, SIGNAL( loadProjectEnded() ), mIface, SIGNAL( loadProjectEnded() ) );

  show();

  QTimer::singleShot( 0, this, SLOT( loadLastProject() ) );
}

void QgisMobileapp::initDeclarative()
{
  // Register QML custom types
  qmlRegisterType<QgsQuickMapCanvasMap>( "org.qgis", 1, 0, "MapCanvasMap" );
  qmlRegisterUncreatableType<AppInterface>( "org.qgis", 1, 0, "QgisInterface", "QgisInterface is only provided by the environment and cannot be created ad-hoc" );
  qmlRegisterUncreatableType<Settings>( "org.qgis", 1, 0, "Settings", "" );
  qmlRegisterType<FeatureListModel>( "org.qgis", 1, 0, "FeatureListModel" );
  qmlRegisterType<FeatureModel>( "org.qgis", 1, 0, "FeatureModel" );
  qmlRegisterType<FeatureListModelSelection>( "org.qgis", 1, 0, "FeatureListModelSelection" );
  qmlRegisterType<FeatureListModelHighlight>( "org.qgis", 1, 0, "FeatureListModelHighlight" );

  // Calculate device pixels
  int dpiX = QApplication::desktop()->physicalDpiX();
  int dpiY = QApplication::desktop()->physicalDpiY();
  int dpi = dpiX < dpiY ? dpiX : dpiY; // In case of asymetrical DPI. Improbable
  float dp = dpi * 0.00768443;

  // Register some globally available variables
  rootContext()->setContextProperty( "dp", dp );
  rootContext()->setContextProperty( "project", QgsProject::instance() );
  rootContext()->setContextProperty( "iface", mIface );
  rootContext()->setContextProperty( "featureListModel", &mFeatureListModel );
  rootContext()->setContextProperty( "settings", &mSettings );
}

void QgisMobileapp::identifyFeatures( const QPointF& point )
{
  QgsMapToolIdentify identify( mMapCanvas );
  QList<QgsMapToolIdentify::IdentifyResult> results = identify.identify( point.x(), point.y(), QgsMapToolIdentify::TopDownAll );

  Q_FOREACH( const QgsMapToolIdentify::IdentifyResult& res, results )
  {
    qDebug() << res.mLayer->name() << " >> " << res.mFeature.id();
  }

  mFeatureListModel.setFeatures( results );
  mIface->openFeatureForm();
}

void QgisMobileapp::onReadProject( const QDomDocument& doc )
{
  Q_UNUSED( doc );
  QMap<QgsVectorLayer*, QgsFeatureRequest> requests;
  QSettings().setValue( "/qgis/project/lastProjectFile", QgsProject::instance()->fileName() );
  Q_FOREACH( QgsMapLayer* layer, QgsMapLayerRegistry::instance()->mapLayers() )
  {
    QgsVectorLayer* vl = qobject_cast<QgsVectorLayer*>( layer );
    if ( vl )
    {
      const QVariant itinerary = vl->customProperty( "qgisMobile/itinerary" );
      if ( itinerary.isValid() )
      {
        requests.insert( vl, QgsFeatureRequest().setFilterExpression( itinerary.toString() ) );
      }
    }
  }
  if ( requests.count() )
  {
    qDebug() << QString( "Loading itinerary for %1 layers." ).arg( requests.count() );
    mFeatureListModel.setFeatures( requests );
    mIface->openFeatureForm();
  }
}

void QgisMobileapp::loadLastProject()
{
  QVariant lastProjectFile = QSettings().value( "/qgis/project/lastProjectFile" );
  if ( lastProjectFile.isValid() )
    QgsProject::instance()->read( lastProjectFile.toString() );
}

void QgisMobileapp::openProjectDialog()
{
  QSettings settings;
  QFileDialog dlg( 0, "Open Project", settings.value( "/qgis/lastProjectOpenDir" ).toString(), "QGIS Project Files (*.qgs)" );
  dlg.setWindowState( dlg.windowState() | Qt::WindowMaximized );

  if ( dlg.exec() )
  {
    loadProjectFile( dlg.selectedFiles().first() );
    settings.setValue( "/qgis/lastProjectOpenDir", QFileInfo( dlg.selectedFiles().first() ).absolutePath() );
  }
}

void QgisMobileapp::loadProjectFile( const QString& path )
{
  emit loadProjectStarted( path );
  QgsProject::instance()->read( path );
  emit loadProjectEnded();
}

QgisMobileapp::~QgisMobileapp()
{
  delete QgsEditorWidgetRegistry::instance();
  delete QgsProject::instance();
}
