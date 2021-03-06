/***************************************************************************
                              qgsexpression.cpp
                             -------------------
    begin                : August 2011
    copyright            : (C) 2011 Martin Dobias
    email                : wonder.sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsexpression.h"
#include "qgsexpressionfunction.h"
#include "qgsexpressionprivate.h"
#include "qgsexpressionnodeimpl.h"
#include "qgsfeaturerequest.h"
#include "qgscolorramp.h"
#include "qgslogger.h"
#include "qgsexpressioncontext.h"
#include "qgsgeometry.h"
#include "qgsproject.h"


// from parser
extern QgsExpressionNode *parseExpression( const QString &str, QString &parserErrorMsg, QList<QgsExpression::ParserError> &parserErrors );

///////////////////////////////////////////////
// QVariant checks and conversions

///////////////////////////////////////////////
// evaluation error macros

///////////////////////////////////////////////
// functions

//////

bool QgsExpression::registerFunction( QgsExpressionFunction *function, bool transferOwnership )
{
  int fnIdx = functionIndex( function->name() );
  if ( fnIdx != -1 )
  {
    return false;
  }
  QgsExpression::sFunctions.append( function );
  if ( transferOwnership )
    QgsExpression::sOwnedFunctions.append( function );
  return true;
}

bool QgsExpression::unregisterFunction( const QString &name )
{
  // You can never override the built in functions.
  if ( QgsExpression::BuiltinFunctions().contains( name ) )
  {
    return false;
  }
  int fnIdx = functionIndex( name );
  if ( fnIdx != -1 )
  {
    QgsExpression::sFunctions.removeAt( fnIdx );
    return true;
  }
  return false;
}

void QgsExpression::cleanRegisteredFunctions()
{
  qDeleteAll( QgsExpression::sOwnedFunctions );
  QgsExpression::sOwnedFunctions.clear();
}

QStringList QgsExpression::sBuiltinFunctions;

const QStringList &QgsExpression::BuiltinFunctions()
{
  if ( sBuiltinFunctions.isEmpty() )
  {
    Functions();  // this method builds the gmBuiltinFunctions as well
  }
  return sBuiltinFunctions;
}

QList<QgsExpressionFunction *> QgsExpression::sFunctions;
QList<QgsExpressionFunction *> QgsExpression::sOwnedFunctions;

bool QgsExpression::checkExpression( const QString &text, const QgsExpressionContext *context, QString &errorMessage )
{
  QgsExpression exp( text );
  exp.prepare( context );
  errorMessage = exp.parserErrorString();
  return !exp.hasParserError();
}

void QgsExpression::setExpression( const QString &expression )
{
  detach();
  d->mRootNode = ::parseExpression( expression, d->mParserErrorString, d->mParserErrors );
  d->mEvalErrorString = QString();
  d->mExp = expression;
}

QString QgsExpression::expression() const
{
  if ( !d->mExp.isNull() )
    return d->mExp;
  else
    return dump();
}

QString QgsExpression::quotedColumnRef( QString name )
{
  return QStringLiteral( "\"%1\"" ).arg( name.replace( '\"', QLatin1String( "\"\"" ) ) );
}

QString QgsExpression::quotedString( QString text )
{
  text.replace( '\'', QLatin1String( "''" ) );
  text.replace( '\\', QLatin1String( "\\\\" ) );
  text.replace( '\n', QLatin1String( "\\n" ) );
  text.replace( '\t', QLatin1String( "\\t" ) );
  return QStringLiteral( "'%1'" ).arg( text );
}

QString QgsExpression::quotedValue( const QVariant &value )
{
  return quotedValue( value, value.type() );
}

QString QgsExpression::quotedValue( const QVariant &value, QVariant::Type type )
{
  if ( value.isNull() )
    return QStringLiteral( "NULL" );

  switch ( type )
  {
    case QVariant::Int:
    case QVariant::LongLong:
    case QVariant::Double:
      return value.toString();

    case QVariant::Bool:
      return value.toBool() ? QStringLiteral( "TRUE" ) : QStringLiteral( "FALSE" );

    case QVariant::List:
    {
      QStringList quotedValues;
      const QVariantList values = value.toList();
      for ( const QVariant &v : values )
      {
        quotedValues += quotedValue( v );
      }
      return QStringLiteral( "array( %1 )" ).arg( quotedValues.join( QStringLiteral( ", " ) ) );
    }

    default:
    case QVariant::String:
      return quotedString( value.toString() );
  }

}

bool QgsExpression::isFunctionName( const QString &name )
{
  return functionIndex( name ) != -1;
}

int QgsExpression::functionIndex( const QString &name )
{
  int count = functionCount();
  for ( int i = 0; i < count; i++ )
  {
    if ( QString::compare( name, QgsExpression::Functions()[i]->name(), Qt::CaseInsensitive ) == 0 )
      return i;
    const QStringList aliases = QgsExpression::Functions()[i]->aliases();
    for ( const QString &alias : aliases )
    {
      if ( QString::compare( name, alias, Qt::CaseInsensitive ) == 0 )
        return i;
    }
  }
  return -1;
}

int QgsExpression::functionCount()
{
  return Functions().size();
}


QgsExpression::QgsExpression( const QString &expr )
  : d( new QgsExpressionPrivate )
{
  d->mRootNode = ::parseExpression( expr, d->mParserErrorString, d->mParserErrors );
  d->mExp = expr;
  Q_ASSERT( !d->mParserErrorString.isNull() || d->mRootNode );
}

QgsExpression::QgsExpression( const QgsExpression &other )
  : d( other.d )
{
  d->ref.ref();
}

QgsExpression &QgsExpression::operator=( const QgsExpression &other )
{
  if ( !d->ref.deref() )
  {
    delete d;
  }

  d = other.d;
  d->ref.ref();
  return *this;
}

QgsExpression::operator QString() const
{
  return d->mExp;
}

QgsExpression::QgsExpression()
  : d( new QgsExpressionPrivate )
{
}

QgsExpression::~QgsExpression()
{
  Q_ASSERT( d );
  if ( !d->ref.deref() )
    delete d;
}

bool QgsExpression::operator==( const QgsExpression &other ) const
{
  return ( d == other.d || d->mExp == other.d->mExp );
}

bool QgsExpression::isValid() const
{
  return d->mRootNode;
}

bool QgsExpression::hasParserError() const
{
  return d->mParserErrors.count() > 0;
}

QString QgsExpression::parserErrorString() const
{
  return d->mParserErrorString;
}

QList<QgsExpression::ParserError> QgsExpression::parserErrors() const
{
  return d->mParserErrors;
}

QSet<QString> QgsExpression::referencedColumns() const
{
  if ( !d->mRootNode )
    return QSet<QString>();

  return d->mRootNode->referencedColumns();
}

QSet<QString> QgsExpression::referencedVariables() const
{
  if ( !d->mRootNode )
    return QSet<QString>();

  return d->mRootNode->referencedVariables();
}

QSet<QString> QgsExpression::referencedFunctions() const
{
  if ( !d->mRootNode )
    return QSet<QString>();

  return d->mRootNode->referencedFunctions();
}

QSet<int> QgsExpression::referencedAttributeIndexes( const QgsFields &fields ) const
{
  if ( !d->mRootNode )
    return QSet<int>();

  const QSet<QString> referencedFields = d->mRootNode->referencedColumns();
  QSet<int> referencedIndexes;

  for ( const QString &fieldName : referencedFields )
  {
    if ( fieldName == QgsFeatureRequest::ALL_ATTRIBUTES )
    {
      referencedIndexes = fields.allAttributesList().toSet();
      break;
    }
    referencedIndexes << fields.lookupField( fieldName );
  }

  return referencedIndexes;
}

bool QgsExpression::needsGeometry() const
{
  if ( !d->mRootNode )
    return false;
  return d->mRootNode->needsGeometry();
}

void QgsExpression::initGeomCalculator()
{
  if ( d->mCalc )
    return;

  // Use planimetric as default
  d->mCalc = std::shared_ptr<QgsDistanceArea>( new QgsDistanceArea() );
}

void QgsExpression::detach()
{
  Q_ASSERT( d );

  if ( d->ref > 1 )
  {
    ( void )d->ref.deref();

    d = new QgsExpressionPrivate( *d );
  }
}

void QgsExpression::setGeomCalculator( const QgsDistanceArea *calc )
{
  detach();
  if ( calc )
    d->mCalc = std::shared_ptr<QgsDistanceArea>( new QgsDistanceArea( *calc ) );
  else
    d->mCalc.reset();
}

bool QgsExpression::prepare( const QgsExpressionContext *context )
{
  detach();
  d->mEvalErrorString = QString();
  if ( !d->mRootNode )
  {
    //re-parse expression. Creation of QgsExpressionContexts may have added extra
    //known functions since this expression was created, so we have another try
    //at re-parsing it now that the context must have been created
    d->mRootNode = ::parseExpression( d->mExp, d->mParserErrorString, d->mParserErrors );
  }

  if ( !d->mRootNode )
  {
    d->mEvalErrorString = tr( "No root node! Parsing failed?" );
    return false;
  }

  return d->mRootNode->prepare( this, context );
}

QVariant QgsExpression::evaluate()
{
  d->mEvalErrorString = QString();
  if ( !d->mRootNode )
  {
    d->mEvalErrorString = tr( "No root node! Parsing failed?" );
    return QVariant();
  }

  return d->mRootNode->eval( this, static_cast<const QgsExpressionContext *>( nullptr ) );
}

QVariant QgsExpression::evaluate( const QgsExpressionContext *context )
{
  d->mEvalErrorString = QString();
  if ( !d->mRootNode )
  {
    d->mEvalErrorString = tr( "No root node! Parsing failed?" );
    return QVariant();
  }

  return d->mRootNode->eval( this, context );
}

bool QgsExpression::hasEvalError() const
{
  return !d->mEvalErrorString.isNull();
}

QString QgsExpression::evalErrorString() const
{
  return d->mEvalErrorString;
}

void QgsExpression::setEvalErrorString( const QString &str )
{
  d->mEvalErrorString = str;
}

QString QgsExpression::dump() const
{
  if ( !d->mRootNode )
    return QString();

  return d->mRootNode->dump();
}

QgsDistanceArea *QgsExpression::geomCalculator()
{
  return d->mCalc.get();
}

QgsUnitTypes::DistanceUnit QgsExpression::distanceUnits() const
{
  return d->mDistanceUnit;
}

void QgsExpression::setDistanceUnits( QgsUnitTypes::DistanceUnit unit )
{
  d->mDistanceUnit = unit;
}

QgsUnitTypes::AreaUnit QgsExpression::areaUnits() const
{
  return d->mAreaUnit;
}

void QgsExpression::setAreaUnits( QgsUnitTypes::AreaUnit unit )
{
  d->mAreaUnit = unit;
}

QString QgsExpression::replaceExpressionText( const QString &action, const QgsExpressionContext *context, const QgsDistanceArea *distanceArea )
{
  QString expr_action;

  int index = 0;
  while ( index < action.size() )
  {
    QRegExp rx = QRegExp( "\\[%([^\\]]+)%\\]" );

    int pos = rx.indexIn( action, index );
    if ( pos < 0 )
      break;

    int start = index;
    index = pos + rx.matchedLength();
    QString to_replace = rx.cap( 1 ).trimmed();
    QgsDebugMsg( "Found expression: " + to_replace );

    QgsExpression exp( to_replace );
    if ( exp.hasParserError() )
    {
      QgsDebugMsg( "Expression parser error: " + exp.parserErrorString() );
      expr_action += action.midRef( start, index - start );
      continue;
    }

    if ( distanceArea )
    {
      //if QgsDistanceArea specified for area/distance conversion, use it
      exp.setGeomCalculator( distanceArea );
    }

    QVariant result = exp.evaluate( context );

    if ( exp.hasEvalError() )
    {
      QgsDebugMsg( "Expression parser eval error: " + exp.evalErrorString() );
      expr_action += action.midRef( start, index - start );
      continue;
    }

    QgsDebugMsg( "Expression result is: " + result.toString() );
    expr_action += action.mid( start, pos - start ) + result.toString();
  }

  expr_action += action.midRef( index );

  return expr_action;
}

QSet<QString> QgsExpression::referencedVariables( const QString &text )
{
  QSet<QString> variables;
  int index = 0;
  while ( index < text.size() )
  {
    QRegExp rx = QRegExp( "\\[%([^\\]]+)%\\]" );

    int pos = rx.indexIn( text, index );
    if ( pos < 0 )
      break;

    index = pos + rx.matchedLength();
    QString to_replace = rx.cap( 1 ).trimmed();

    QgsExpression exp( to_replace );
    variables.unite( exp.referencedVariables() );
  }

  return variables;
}

double QgsExpression::evaluateToDouble( const QString &text, const double fallbackValue )
{
  bool ok;
  //first test if text is directly convertible to double
  // use system locale: e.g. in German locale, user is presented with numbers "1,23" instead of "1.23" in C locale
  // so we also want to allow user to rewrite it to "5,23" and it is still accepted
  double convertedValue = QLocale().toDouble( text, &ok );
  if ( ok )
  {
    return convertedValue;
  }

  //otherwise try to evaluate as expression
  QgsExpression expr( text );

  QgsExpressionContext context;
  context << QgsExpressionContextUtils::globalScope()
          << QgsExpressionContextUtils::projectScope( QgsProject::instance() );

  QVariant result = expr.evaluate( &context );
  convertedValue = result.toDouble( &ok );
  if ( expr.hasEvalError() || !ok )
  {
    return fallbackValue;
  }
  return convertedValue;
}



QString QgsExpression::helpText( QString name )
{
  QgsExpression::initFunctionHelp();

  if ( !sFunctionHelpTexts.contains( name ) )
    return tr( "function help for %1 missing" ).arg( name );

  const Help &f = sFunctionHelpTexts[ name ];

  name = f.mName;
  if ( f.mType == tr( "group" ) )
    name = group( name );

  name = name.toHtmlEscaped();

  QString helpContents( QStringLiteral( "<h3>%1</h3>\n<div class=\"description\"><p>%2</p></div>" )
                        .arg( tr( "%1 %2" ).arg( f.mType, name ),
                              f.mDescription ) );

  for ( const HelpVariant &v : qgis::as_const( f.mVariants ) )
  {
    if ( f.mVariants.size() > 1 )
    {
      helpContents += QStringLiteral( "<h3>%1</h3>\n<div class=\"description\">%2</p></div>" ).arg( v.mName, v.mDescription );
    }

    if ( f.mType != tr( "group" ) && f.mType != tr( "expression" ) )
      helpContents += QStringLiteral( "<h4>%1</h4>\n<div class=\"syntax\">\n" ).arg( tr( "Syntax" ) );

    if ( f.mType == tr( "operator" ) )
    {
      if ( v.mArguments.size() == 1 )
      {
        helpContents += QStringLiteral( "<code><span class=\"functionname\">%1</span> <span class=\"argument\">%2</span></code>" )
                        .arg( name, v.mArguments[0].mArg );
      }
      else if ( v.mArguments.size() == 2 )
      {
        helpContents += QStringLiteral( "<code><span class=\"argument\">%1</span> <span class=\"functionname\">%2</span> <span class=\"argument\">%3</span></code>" )
                        .arg( v.mArguments[0].mArg, name, v.mArguments[1].mArg );
      }
    }
    else if ( f.mType != tr( "group" ) && f.mType != tr( "expression" ) )
    {
      helpContents += QStringLiteral( "<code><span class=\"functionname\">%1</span>" ).arg( name );

      bool hasOptionalArgs = false;

      if ( f.mType == tr( "function" ) && ( f.mName[0] != '$' || !v.mArguments.isEmpty() || v.mVariableLenArguments ) )
      {
        helpContents += '(';

        QString delim;
        for ( const HelpArg &a : qgis::as_const( v.mArguments ) )
        {
          if ( !a.mDescOnly )
          {
            if ( a.mOptional )
            {
              hasOptionalArgs = true;
              helpContents += QStringLiteral( "[" );
            }

            helpContents += delim;
            helpContents += QStringLiteral( "<span class=\"argument\">%2%3</span>" ).arg(
                              a.mArg,
                              a.mDefaultVal.isEmpty() ? QString() : '=' + a.mDefaultVal
                            );

            if ( a.mOptional )
              helpContents += QStringLiteral( "]" );
          }
          delim = QStringLiteral( "," );
        }

        if ( v.mVariableLenArguments )
        {
          helpContents += QChar( 0x2026 );
        }

        helpContents += ')';
      }

      helpContents += QLatin1String( "</code>" );

      if ( hasOptionalArgs )
      {
        helpContents += QLatin1String( "<br/><br/>" ) + tr( "[ ] marks optional components" );
      }
    }

    if ( !v.mArguments.isEmpty() )
    {
      helpContents += QStringLiteral( "<h4>%1</h4>\n<div class=\"arguments\">\n<table>" ).arg( tr( "Arguments" ) );

      for ( const HelpArg &a : qgis::as_const( v.mArguments ) )
      {
        if ( a.mSyntaxOnly )
          continue;

        helpContents += QStringLiteral( "<tr><td class=\"argument\">%1</td><td>%2</td></tr>" ).arg( a.mArg, a.mDescription );
      }

      helpContents += QLatin1String( "</table>\n</div>\n" );
    }

    if ( !v.mExamples.isEmpty() )
    {
      helpContents += QStringLiteral( "<h4>%1</h4>\n<div class=\"examples\">\n<ul>\n" ).arg( tr( "Examples" ) );

      for ( const HelpExample &e : qgis::as_const( v.mExamples ) )
      {
        helpContents += "<li><code>" + e.mExpression + "</code> &rarr; <code>" + e.mReturns + "</code>";

        if ( !e.mNote.isEmpty() )
          helpContents += QStringLiteral( " (%1)" ).arg( e.mNote );

        helpContents += QLatin1String( "</li>\n" );
      }

      helpContents += QLatin1String( "</ul>\n</div>\n" );
    }

    if ( !v.mNotes.isEmpty() )
    {
      helpContents += QStringLiteral( "<h4>%1</h4>\n<div class=\"notes\"><p>%2</p></div>\n" ).arg( tr( "Notes" ), v.mNotes );
    }
  }

  return helpContents;
}

QHash<QString, QString> QgsExpression::sVariableHelpTexts;

void QgsExpression::initVariableHelp()
{
  if ( !sVariableHelpTexts.isEmpty() )
    return;

  //global variables
  sVariableHelpTexts.insert( QStringLiteral( "qgis_version" ), QCoreApplication::translate( "variable_help", "Current QGIS version string." ) );
  sVariableHelpTexts.insert( QStringLiteral( "qgis_version_no" ), QCoreApplication::translate( "variable_help", "Current QGIS version number." ) );
  sVariableHelpTexts.insert( QStringLiteral( "qgis_release_name" ), QCoreApplication::translate( "variable_help", "Current QGIS release name." ) );
  sVariableHelpTexts.insert( QStringLiteral( "qgis_os_name" ), QCoreApplication::translate( "variable_help", "Operating system name, e.g., 'windows', 'linux' or 'osx'." ) );
  sVariableHelpTexts.insert( QStringLiteral( "qgis_platform" ), QCoreApplication::translate( "variable_help", "QGIS platform, e.g., 'desktop' or 'server'." ) );
  sVariableHelpTexts.insert( QStringLiteral( "user_account_name" ), QCoreApplication::translate( "variable_help", "Current user's operating system account name." ) );
  sVariableHelpTexts.insert( QStringLiteral( "user_full_name" ), QCoreApplication::translate( "variable_help", "Current user's operating system user name (if available)." ) );

  //project variables
  sVariableHelpTexts.insert( QStringLiteral( "project_title" ), QCoreApplication::translate( "variable_help", "Title of current project." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_path" ), QCoreApplication::translate( "variable_help", "Full path (including file name) of current project." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_folder" ), QCoreApplication::translate( "variable_help", "Folder for current project." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_filename" ), QCoreApplication::translate( "variable_help", "Filename of current project." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_basename" ), QCoreApplication::translate( "variable_help", "Base name of current project's filename (without path and extension)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_home" ), QCoreApplication::translate( "variable_help", "Home path of current project." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_crs" ), QCoreApplication::translate( "variable_help", "Coordinate reference system of project (e.g., 'EPSG:4326')." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_crs_definition" ), QCoreApplication::translate( "variable_help", "Coordinate reference system of project (full definition)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_author" ), QCoreApplication::translate( "variable_help", "Project author, taken from project metadata." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_abstract" ), QCoreApplication::translate( "variable_help", "Project abstract, taken from project metadata." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_creation_date" ), QCoreApplication::translate( "variable_help", "Project creation date, taken from project metadata." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_identifier" ), QCoreApplication::translate( "variable_help", "Project identifier, taken from project metadata." ) );
  sVariableHelpTexts.insert( QStringLiteral( "project_keywords" ), QCoreApplication::translate( "variable_help", "Project keywords, taken from project metadata." ) );

  //layer variables
  sVariableHelpTexts.insert( QStringLiteral( "layer_name" ), QCoreApplication::translate( "variable_help", "Name of current layer." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layer_id" ), QCoreApplication::translate( "variable_help", "ID of current layer." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layer" ), QCoreApplication::translate( "variable_help", "The current layer." ) );

  //composition variables
  sVariableHelpTexts.insert( QStringLiteral( "layout_name" ), QCoreApplication::translate( "variable_help", "Name of composition." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layout_numpages" ), QCoreApplication::translate( "variable_help", "Number of pages in composition." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layout_page" ), QCoreApplication::translate( "variable_help", "Current page number in composition." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layout_pageheight" ), QCoreApplication::translate( "variable_help", "Composition page height in mm." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layout_pagewidth" ), QCoreApplication::translate( "variable_help", "Composition page width in mm." ) );
  sVariableHelpTexts.insert( QStringLiteral( "layout_dpi" ), QCoreApplication::translate( "variable_help", "Composition resolution (DPI)." ) );

  //atlas variables
  sVariableHelpTexts.insert( QStringLiteral( "atlas_layerid" ), QCoreApplication::translate( "variable_help", "Current atlas coverage layer ID." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_layername" ), QCoreApplication::translate( "variable_help", "Current atlas coverage layer name." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_totalfeatures" ), QCoreApplication::translate( "variable_help", "Total number of features in atlas." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_featurenumber" ), QCoreApplication::translate( "variable_help", "Current atlas feature number." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_filename" ), QCoreApplication::translate( "variable_help", "Current atlas file name." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_pagename" ), QCoreApplication::translate( "variable_help", "Current atlas page name." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_feature" ), QCoreApplication::translate( "variable_help", "Current atlas feature (as feature object)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_featureid" ), QCoreApplication::translate( "variable_help", "Current atlas feature ID." ) );
  sVariableHelpTexts.insert( QStringLiteral( "atlas_geometry" ), QCoreApplication::translate( "variable_help", "Current atlas feature geometry." ) );

  //layout item variables
  sVariableHelpTexts.insert( QStringLiteral( "item_id" ), QCoreApplication::translate( "variable_help", "Layout item user ID (not necessarily unique)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "item_uuid" ), QCoreApplication::translate( "variable_help", "layout item unique ID." ) );
  sVariableHelpTexts.insert( QStringLiteral( "item_left" ), QCoreApplication::translate( "variable_help", "Left position of layout item (in mm)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "item_top" ), QCoreApplication::translate( "variable_help", "Top position of layout item (in mm)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "item_width" ), QCoreApplication::translate( "variable_help", "Width of layout item (in mm)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "item_height" ), QCoreApplication::translate( "variable_help", "Height of layout item (in mm)." ) );

  //map settings item variables
  sVariableHelpTexts.insert( QStringLiteral( "map_id" ), QCoreApplication::translate( "variable_help", "ID of current map destination. This will be 'canvas' for canvas renders, and the item ID for layout map renders." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_rotation" ), QCoreApplication::translate( "variable_help", "Current rotation of map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_scale" ), QCoreApplication::translate( "variable_help", "Current scale of map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_extent" ), QCoreApplication::translate( "variable_help", "Geometry representing the current extent of the map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_extent_center" ), QCoreApplication::translate( "variable_help", "Center of map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_extent_width" ), QCoreApplication::translate( "variable_help", "Width of map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_extent_height" ), QCoreApplication::translate( "variable_help", "Height of map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_crs" ), QCoreApplication::translate( "variable_help", "Coordinate reference system of map (e.g., 'EPSG:4326')." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_crs_definition" ), QCoreApplication::translate( "variable_help", "Coordinate reference system of map (full definition)." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_units" ), QCoreApplication::translate( "variable_help", "Units for map measurements." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_layer_ids" ), QCoreApplication::translate( "variable_help", "List of map layer IDs visible in the map." ) );
  sVariableHelpTexts.insert( QStringLiteral( "map_layers" ), QCoreApplication::translate( "variable_help", "List of map layers visible in the map." ) );

  sVariableHelpTexts.insert( QStringLiteral( "row_number" ), QCoreApplication::translate( "variable_help", "Stores the number of the current row." ) );
  sVariableHelpTexts.insert( QStringLiteral( "grid_number" ), QCoreApplication::translate( "variable_help", "Current grid annotation value." ) );
  sVariableHelpTexts.insert( QStringLiteral( "grid_axis" ), QCoreApplication::translate( "variable_help", "Current grid annotation axis (e.g., 'x' for longitude, 'y' for latitude)." ) );

  // map canvas item variables
  sVariableHelpTexts.insert( QStringLiteral( "canvas_cursor_point" ), QCoreApplication::translate( "variable_help", "Last cursor position on the canvas in the project's geographical coordinates." ) );

  // map tool capture variables
  sVariableHelpTexts.insert( QStringLiteral( "snapping_results" ), QCoreApplication::translate( "variable_help",
                             "<p>An array with an item for each snapped point.</p>"
                             "<p>Each item is a map with the following keys:</p>"
                             "<dl>"
                             "<dt>valid</dt><dd>Boolean that indicates if the snapping result is valid</dd>"
                             "<dt>layer</dt><dd>The layer on which the snapped feature is</dd>"
                             "<dt>feature_id</dt><dd>The feature id of the snapped feature</dd>"
                             "<dt>vertex_index</dt><dd>The index of the snapped vertex</dd>"
                             "<dt>distance</dt><dd>The distance between the mouse cursor and the snapped point at the time of snapping</dd>"
                             "</dl>" ) );


  //symbol variables
  sVariableHelpTexts.insert( QStringLiteral( "geometry_part_count" ), QCoreApplication::translate( "variable_help", "Number of parts in rendered feature's geometry." ) );
  sVariableHelpTexts.insert( QStringLiteral( "geometry_part_num" ), QCoreApplication::translate( "variable_help", "Current geometry part number for feature being rendered." ) );
  sVariableHelpTexts.insert( QStringLiteral( "geometry_point_count" ), QCoreApplication::translate( "variable_help", "Number of points in the rendered geometry's part. It is only meaningful for line geometries and for symbol layers that set this variable." ) );
  sVariableHelpTexts.insert( QStringLiteral( "geometry_point_num" ), QCoreApplication::translate( "variable_help", "Current point number in the rendered geometry's part. It is only meaningful for line geometries and for symbol layers that set this variable." ) );

  sVariableHelpTexts.insert( QStringLiteral( "symbol_color" ), QCoreApplication::translate( "symbol_color", "Color of symbol used to render the feature." ) );
  sVariableHelpTexts.insert( QStringLiteral( "symbol_angle" ), QCoreApplication::translate( "symbol_angle", "Angle of symbol used to render the feature (valid for marker symbols only)." ) );

  //cluster variables
  sVariableHelpTexts.insert( QStringLiteral( "cluster_color" ), QCoreApplication::translate( "cluster_color", "Color of symbols within a cluster, or NULL if symbols have mixed colors." ) );
  sVariableHelpTexts.insert( QStringLiteral( "cluster_size" ), QCoreApplication::translate( "cluster_size", "Number of symbols contained within a cluster." ) );

  //processing variables
  sVariableHelpTexts.insert( QStringLiteral( "algorithm_id" ), QCoreApplication::translate( "algorithm_id", "Unique ID for algorithm." ) );

  //provider notification
  sVariableHelpTexts.insert( QStringLiteral( "notification_message" ), QCoreApplication::translate( "notification_message", "Content of the notification message sent by the provider (available only for actions triggered by provider notifications)." ) );

  //form context variable
  sVariableHelpTexts.insert( QStringLiteral( "current_geometry" ), QCoreApplication::translate( "current_geometry", "Represents the geometry of the feature currently being edited in the form or the table row. Can be used in a form/row context to filter the related features." ) );
  sVariableHelpTexts.insert( QStringLiteral( "current_feature" ), QCoreApplication::translate( "current_feature", "Represents the feature currently being edited in the form or the table row. Can be used in a form/row context to filter the related features." ) );
}

QString QgsExpression::variableHelpText( const QString &variableName )
{
  QgsExpression::initVariableHelp();
  return sVariableHelpTexts.value( variableName, QString() );
}

QString QgsExpression::formatVariableHelp( const QString &description, bool showValue, const QVariant &value )
{
  QString text = !description.isEmpty() ? QStringLiteral( "<p>%1</p>" ).arg( description ) : QString();
  if ( showValue )
  {
    QString valueString;
    if ( !value.isValid() )
    {
      valueString = QCoreApplication::translate( "variable_help", "not set" );
    }
    else
    {
      valueString = QStringLiteral( "<b>%1</b>" ).arg( formatPreviewString( value ) );
    }
    text.append( QCoreApplication::translate( "variable_help", "<p>Current value: %1</p>" ).arg( valueString ) );
  }
  return text;
}

QHash<QString, QString> QgsExpression::sGroups;

QString QgsExpression::group( const QString &name )
{
  if ( sGroups.isEmpty() )
  {
    sGroups.insert( QStringLiteral( "General" ), tr( "General" ) );
    sGroups.insert( QStringLiteral( "Operators" ), tr( "Operators" ) );
    sGroups.insert( QStringLiteral( "Conditionals" ), tr( "Conditionals" ) );
    sGroups.insert( QStringLiteral( "Fields and Values" ), tr( "Fields and Values" ) );
    sGroups.insert( QStringLiteral( "Math" ), tr( "Math" ) );
    sGroups.insert( QStringLiteral( "Conversions" ), tr( "Conversions" ) );
    sGroups.insert( QStringLiteral( "Date and Time" ), tr( "Date and Time" ) );
    sGroups.insert( QStringLiteral( "String" ), tr( "String" ) );
    sGroups.insert( QStringLiteral( "Color" ), tr( "Color" ) );
    sGroups.insert( QStringLiteral( "GeometryGroup" ), tr( "Geometry" ) );
    sGroups.insert( QStringLiteral( "Record" ), tr( "Record" ) );
    sGroups.insert( QStringLiteral( "Variables" ), tr( "Variables" ) );
    sGroups.insert( QStringLiteral( "Fuzzy Matching" ), tr( "Fuzzy Matching" ) );
    sGroups.insert( QStringLiteral( "Recent (%1)" ), tr( "Recent (%1)" ) );
  }

  //return the translated name for this group. If group does not
  //have a translated name in the gGroups hash, return the name
  //unchanged
  return sGroups.value( name, name );
}

QString QgsExpression::formatPreviewString( const QVariant &value, const bool htmlOutput )
{
  static const int MAX_PREVIEW = 60;

  const QString startToken = htmlOutput ? QStringLiteral( "<i>&lt;" ) : QStringLiteral( "<" );
  const QString endToken = htmlOutput ? QStringLiteral( "&gt;</i>" ) : QStringLiteral( ">" );

  if ( value.canConvert<QgsGeometry>() )
  {
    //result is a geometry
    QgsGeometry geom = value.value<QgsGeometry>();
    if ( geom.isNull() )
      return startToken + tr( "empty geometry" ) + endToken;
    else
      return startToken + tr( "geometry: %1" ).arg( QgsWkbTypes::displayString( geom.constGet()->wkbType() ) )
             + endToken;
  }
  else if ( value.value< QgsWeakMapLayerPointer >().data() )
  {
    return startToken + tr( "map layer" ) + endToken;
  }
  else if ( !value.isValid() )
  {
    return htmlOutput ? tr( "<i>NULL</i>" ) : QString();
  }
  else if ( value.canConvert< QgsFeature >() )
  {
    //result is a feature
    QgsFeature feat = value.value<QgsFeature>();
    return startToken + tr( "feature: %1" ).arg( feat.id() ) + endToken;
  }
  else if ( value.canConvert< QgsInterval >() )
  {
    //result is a feature
    QgsInterval interval = value.value<QgsInterval>();
    return startToken + tr( "interval: %1 days" ).arg( interval.days() ) + endToken;
  }
  else if ( value.canConvert< QgsGradientColorRamp >() )
  {
    return startToken + tr( "gradient ramp" ) + endToken;
  }
  else if ( value.type() == QVariant::Date )
  {
    QDate dt = value.toDate();
    return startToken + tr( "date: %1" ).arg( dt.toString( QStringLiteral( "yyyy-MM-dd" ) ) ) + endToken;
  }
  else if ( value.type() == QVariant::Time )
  {
    QTime tm = value.toTime();
    return startToken + tr( "time: %1" ).arg( tm.toString( QStringLiteral( "hh:mm:ss" ) ) ) + endToken;
  }
  else if ( value.type() == QVariant::DateTime )
  {
    QDateTime dt = value.toDateTime();
    return startToken + tr( "datetime: %1" ).arg( dt.toString( QStringLiteral( "yyyy-MM-dd hh:mm:ss" ) ) ) + endToken;
  }
  else if ( value.type() == QVariant::String )
  {
    const QString previewString = value.toString();
    if ( previewString.length() > MAX_PREVIEW + 3 )
    {
      return tr( "'%1…'" ).arg( previewString.left( MAX_PREVIEW ) );
    }
    else
    {
      return '\'' + previewString + '\'';
    }
  }
  else if ( value.type() == QVariant::Map )
  {
    QString mapStr = QStringLiteral( "{" );
    const QVariantMap map = value.toMap();
    QString separator;
    for ( QVariantMap::const_iterator it = map.constBegin(); it != map.constEnd(); ++it )
    {
      mapStr.append( separator );
      if ( separator.isEmpty() )
        separator = QStringLiteral( "," );

      mapStr.append( QStringLiteral( " '%1': %2" ).arg( it.key(), formatPreviewString( it.value(), htmlOutput ) ) );
      if ( mapStr.length() > MAX_PREVIEW - 3 )
      {
        mapStr = tr( "%1…" ).arg( mapStr.left( MAX_PREVIEW - 2 ) );
        break;
      }
    }
    if ( !map.empty() )
      mapStr += QStringLiteral( " " );
    mapStr += QStringLiteral( "}" );
    return mapStr;
  }
  else if ( value.type() == QVariant::List || value.type() == QVariant::StringList )
  {
    QString listStr = QStringLiteral( "[" );
    const QVariantList list = value.toList();
    QString separator;
    for ( const QVariant &arrayValue : list )
    {
      listStr.append( separator );
      if ( separator.isEmpty() )
        separator = QStringLiteral( "," );

      listStr.append( " " );
      listStr.append( formatPreviewString( arrayValue, htmlOutput ) );
      if ( listStr.length() > MAX_PREVIEW - 3 )
      {
        listStr = QString( tr( "%1…" ) ).arg( listStr.left( MAX_PREVIEW - 2 ) );
        break;
      }
    }
    if ( !list.empty() )
      listStr += QStringLiteral( " " );
    listStr += QStringLiteral( "]" );
    return listStr;
  }
  else
  {
    return value.toString();
  }
}

QString QgsExpression::createFieldEqualityExpression( const QString &fieldName, const QVariant &value )
{
  QString expr;

  if ( value.isNull() )
    expr = QStringLiteral( "%1 IS NULL" ).arg( quotedColumnRef( fieldName ) );
  else
    expr = QStringLiteral( "%1 = %2" ).arg( quotedColumnRef( fieldName ), quotedValue( value ) );

  return expr;
}

const QgsExpressionNode *QgsExpression::rootNode() const
{
  return d->mRootNode;
}

bool QgsExpression::isField() const
{
  return d->mRootNode && d->mRootNode->nodeType() == QgsExpressionNode::ntColumnRef;
}

QList<const QgsExpressionNode *> QgsExpression::nodes() const
{
  if ( !d->mRootNode )
    return QList<const QgsExpressionNode *>();

  return d->mRootNode->nodes();
}



