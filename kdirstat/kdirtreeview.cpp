/*
 *   File name:	kdirtreeview.cpp
 *   Summary:	High level classes for KDirStat
 *   License:	LGPL - See file COPYING.LIB for details.
 *   Author:	Stefan Hundhammer <sh@suse.de>
 *
 *   Updated:	2001-06-21
 *
 *   $Id: kdirtreeview.cpp,v 1.2 2001/07/01 17:12:47 alexannika Exp $
 *
 */

#include <time.h>

#include <qtimer.h>
#include <qcolor.h>
#include <qheader.h>

#include <kapp.h>
#include <klocale.h>
#include <kglobal.h>
#include <kglobalsettings.h>
#include <kdebug.h>

#include "kdirtreeview.h"
#include "kpacman.h"
#include "qtreemap.h"
#include "qtreemapwindow.h"

using namespace KDirStat;


KDirTreeView::KDirTreeView( QWidget * parent )
    : KDirTreeViewParentClass( parent )
{
    _tree		= 0;
    _updateTimer	= 0;
    _openLevel		= 1;
    _doLazyClone	= true;
    _doPacManAnimation	= false;
    _updateInterval	= 333;	// millisec
    _readJobsCol	= -1;
    setRootIsDecorated( false );

    int numCol = 0;
    addColumn( i18n( "Name"			) ); _nameCol		= numCol;
    _iconCol = numCol++;
    addColumn( i18n( "Subtree Percentage" 	) ); _percentBarCol	= numCol++;
    addColumn( i18n( "Percentage"		) ); _percentNumCol	= numCol++;
    addColumn( i18n( "Subtree Total"		) ); _totalSizeCol	= numCol++;
    _workingStatusCol = _totalSizeCol;
    addColumn( i18n( "Own Size"			) ); _ownSizeCol	= numCol++;
    addColumn( i18n( "Items"			) ); _totalItemsCol	= numCol++;
    addColumn( i18n( "Files"			) ); _totalFilesCol	= numCol++;
    addColumn( i18n( "Subdirs"			) ); _totalSubDirsCol	= numCol++;
    addColumn( i18n( "Last Change"		) ); _latestMtimeCol	= numCol++;

    setColumnAlignment ( _totalSizeCol,		AlignRight );
    setColumnAlignment ( _percentNumCol,	AlignRight );
    setColumnAlignment ( _ownSizeCol,		AlignRight );
    setColumnAlignment ( _totalItemsCol,	AlignRight );
    setColumnAlignment ( _totalFilesCol,	AlignRight );
    setColumnAlignment ( _totalSubDirsCol,	AlignRight );

    setSorting( _totalSizeCol );


#define loadIcon(ICON)	KGlobal::iconLoader()->loadIcon( (ICON), KIcon::Small )

    _openDirIcon	= loadIcon( "folder_open" 	);
    _closedDirIcon	= loadIcon( "folder"		);
    _openDotEntryIcon	= loadIcon( "folder_orange_open");
    _closedDotEntryIcon	= loadIcon( "folder_orange"	);
    _unreadableDirIcon	= loadIcon( "folder_locked" 	);
    _fileIcon		= loadIcon( "mime_empty"	);
    _symLinkIcon	= loadIcon( "symlink"		);	// The KDE standard link icon is ugly!
    _blockDevIcon	= loadIcon( "blockdevice"	);
    _charDevIcon	= loadIcon( "chardevice"	);
    _fifoIcon		= loadIcon( "socket"		);
    _workingIcon	= loadIcon( "mime_empty"	);
    _readyIcon		= QPixmap();

#undef loadIcon

    // Initialize colors.

    for ( int i=0; i < KDirTreeViewMaxFillColor; i++ )
    {
	_fillColor[i]	= blue;
    }

    _usedFillColors = 1;
    ensureContrast();


#if 0
    // Provide some nice 3D look.

    setFrameStyle ( QFrame::WinPanel | QFrame::Sunken );
    setMargin ( 2 );
    setLineWidth ( 2 );
#endif

   connect ( kapp,	SIGNAL	( kdisplayPaletteChanged()	),
	     this, 	SLOT	( paletteChanged()		) );
	
   _treemap_view=new KDirStat::QTreeMapWindow();
}


KDirTreeView::~KDirTreeView()
{
    if ( _tree )
	delete _tree;

    /*
     * Don't delete _updateTimer here, it's already automatically deleted by Qt!
     * (Since it's derived from QObject and has a QObject parent).
     */
}


void
KDirTreeView::busyDisplay()
{
    if ( _readJobsCol < 0 )
    {
	_readJobsCol = header()->count();
	addColumn( i18n( "Read Jobs" ) );
	setColumnAlignment ( _readJobsCol, AlignRight );
    }
}


void
KDirTreeView::idleDisplay()
{
    if ( _readJobsCol >= 0 )
    {
	removeColumn( _readJobsCol );
	_readJobsCol = -1;
    }

    //_treemap_view->drawTreeMap(_tree->root());
    //_treemap_view->getArea()->setTreeMap((KDirInfo *)_tree->root());


    if(_tree && _tree->root()){
           _treemap_view->getArea()->setTreeMap((Object *)_tree->root());
     }
}


void
KDirTreeView::openURL( KURL url )
{
    // Clean up any old leftovers

    clear();
    _currentDir = "";

    if ( _tree )
	delete _tree;


    // Create new (empty) dir tree

    _tree = new KDirTree();


    // Connect signals

    connect( _tree, SIGNAL( progressInfo    ( const QString & ) ),
	     this,  SLOT  ( sendProgressInfo( const QString & ) ) );

    connect( _tree, SIGNAL( childAdded( KFileInfo * ) ),
	     this,  SLOT  ( addChild  ( KFileInfo * ) ) );

    connect( _tree, SIGNAL( finished() ),
	     this,  SLOT  ( slotFinished() ) );

    connect( _tree, SIGNAL( finalizeLocal( KDirInfo * ) ),
	     this,  SLOT  ( finalizeLocal( KDirInfo * ) ) );


    // Prepare cyclic update

    if ( _updateTimer )
	delete _updateTimer;

    _updateTimer = new QTimer( this );

    if ( _updateTimer )
    {
	_updateTimer->changeInterval( _updateInterval );
	connect( _updateTimer, SIGNAL( timeout() ),
		 this,   SLOT  ( updateSummary() ) );

	connect( _updateTimer, SIGNAL( timeout() ),
		 this,   SLOT  ( sendProgressInfo() ) );
    }


    // Change display to busy state
    
    setSorting( _totalSizeCol );
    busyDisplay();
    emit startingReading();
    

    // Actually do something

    _stopWatch.start();
    _tree->startReading( url );
}


void
KDirTreeView::clear()
{
    KDirTreeViewParentClass::clear();

    // TODO
}


void
KDirTreeView::addChild( KFileInfo *newChild )
{
    if ( newChild->parent() )
    {
	KDirTreeViewItem *cloneParent = locate( newChild->parent(), _doLazyClone );

	if ( cloneParent )
	{
	    // kdDebug() << "Immediately cloning " << newChild->debugUrl() << endl;
	    new KDirTreeViewItem( this, cloneParent, newChild );
	}
	else	// Error
	{
	    if ( ! _doLazyClone )
	    {
		kdError() << k_funcinfo << "Can't find parent view item for "
			  << newChild->debugUrl() << endl;
	    }
	}
    }
    else	// No parent - top level item
    {
	// kdDebug() << "Immediately top level cloning " << newChild->debugUrl() << endl;
	new KDirTreeViewItem( this, newChild );
    }
}


void
KDirTreeView::updateSummary()
{
    KDirTreeViewItem *child = firstChild();

    while ( child )
    {
	child->updateSummary();
	child = child->next();
    }
}


void
KDirTreeView::slotFinished()
{
    emit progressInfo( i18n( "Finished. Elapsed time: %1" )
		       .arg( formatTime( _stopWatch.elapsed(), true ) ) );

    if ( _updateTimer )
    {
	delete _updateTimer;
	_updateTimer = 0;
    }

    idleDisplay();
    updateSummary();

    emit finished();
}


void
KDirTreeView::finalizeLocal( KDirInfo *dir )
{
    if ( dir )
    {
	KDirTreeViewItem *clone = locate( dir,
					  false );	// lazy

	if ( clone )
	    clone->finalizeLocal();
    }
}


void
KDirTreeView::sendProgressInfo( const QString & newCurrentDir )
{
    _currentDir = newCurrentDir;

    emit progressInfo( i18n( "Elapsed time: %1   reading directory %2" )
		       .arg( formatTime( _stopWatch.elapsed() ) )
		       .arg( _currentDir ) );
}


void
KDirTreeView::sendProgressInfo()
{
    sendProgressInfo( _currentDir );
}


KDirTreeViewItem *
KDirTreeView::locate( KFileInfo *wanted, bool lazy )
{
    KDirTreeViewItem *child = firstChild();

    while ( child )
    {
	KDirTreeViewItem *wantedChild = child->locate( wanted, lazy, 0 );

	if ( wantedChild )
	    return wantedChild;
	else
	    child = child->next();
    }

    return 0;
}


const QColor &
KDirTreeView::fillColor( int level ) const
{
    if ( level < 0 )
    {
	level = 0;
	warning ( "KDirTreeView::fillColor(): Invalid argument: %d", level );
    }

    return _fillColor [ level % _usedFillColors ];
}



void
KDirTreeView::setFillColor( int			level,
			    const QColor &	color )
{
    if ( level > 0 && level < KDirTreeViewMaxFillColor )
	_fillColor[ level ] = color;
}



void
KDirTreeView::setUsedFillColors( int usedFillColors )
{
    if ( usedFillColors < 1 )
    {
	kdWarning() << k_funcinfo << "Invalid argument: "<< usedFillColors << endl;
	usedFillColors = 1;
    }
    else if ( usedFillColors >= KDirTreeViewMaxFillColor )
    {
	kdWarning() << k_funcinfo << "Invalid argument: "<< usedFillColors
		    << " (max: " << KDirTreeViewMaxFillColor-1 << ")" << endl;
	usedFillColors = KDirTreeViewMaxFillColor-1;
    }

    _usedFillColors = usedFillColors;
}



void
KDirTreeView::setTreeBackground( const QColor &color )
{
    _treeBackground = color;
    _percentageBarBackground = _treeBackground.dark( 115 );

    QPalette pal = kapp->palette();
    pal.setBrush( QColorGroup::Base, _treeBackground );
    setPalette( pal );
}


void
KDirTreeView::ensureContrast()
{
    if ( colorGroup().base() == white ||
	 colorGroup().base() == black   )
    {
	setTreeBackground( colorGroup().midlight() );
    }
    else
    {
	setTreeBackground( colorGroup().base() );
    }
}


void
KDirTreeView::paletteChanged()
{
    setTreeBackground( KGlobalSettings::baseColor() );
    ensureContrast();
}






KDirTreeViewItem::KDirTreeViewItem	( KDirTreeView *	view,
					  KFileInfo *		orig )
    : QListViewItem( view )
{
    init( view, 0, orig );
}


KDirTreeViewItem::KDirTreeViewItem	( KDirTreeView *	view,
					  KDirTreeViewItem *	parent,
					  KFileInfo *		orig )
    : QListViewItem( parent )
{
    CHECK_PTR( parent );
    init( view, parent, orig );
}


void
KDirTreeViewItem::init	( KDirTreeView *	view,
			  KDirTreeViewItem *	parent,
			  KFileInfo *		orig )
{
    _view 	= view;
    _parent	= parent;
    _orig	= orig;
    _percent	= 0.0;
    _pacMan	= 0;

    if ( _orig->isDotEntry() )
    {
       setText( view->nameCol(), i18n( "<Files>" ) );
       QListViewItem::setOpen ( false );
    }
    else
    {
	setText( view->nameCol(), _orig->name() );

	if ( ! _orig->isDevice() )
	{
	    setText( view->ownSizeCol(), formatSize( _orig->size() ) );
	}

	QListViewItem::setOpen ( _orig->treeLevel() < _view->openLevel() );
	/*
	 * Don't use KDirTreeViewItem::setOpen() here since this might call
	 * KDirTreeViewItem::deferredClone() which would confuse bookkeeping
	 * with addChild() signals that might arrive, too - resulting in double
	 * dot entries.
	 */
    }

    if ( _view->doLazyClone() &&
	 ( _orig->isDir() || _orig->isDotEntry() ) )
    {
	/*
	 * Determine whether or not this item can be opened.
	 *
	 * Normally, Qt handles this very well, but when lazy cloning is in
	 * effect, Qt cannot know whether or not there are children - they may
	 * only be in the original tree until the user tries to open this
	 * item. So let's assume there may be children as long as the directory
	 * is still being read.
	 */
	
	if ( _orig->readState() == KDirQueued ||
	     _orig->readState() == KDirReading  )
	{
	    setExpandable( true );
	}
	else	// KDirFinished, KDirError, KDirAborted
	{
	    setExpandable( _orig->hasChildren() );
	}
    }

    if ( ! parent || parent->isOpen() )
    {
	setIcon();
    }
}


KDirTreeViewItem::~KDirTreeViewItem()
{
    if ( _pacMan )
	delete _pacMan;
}


void
KDirTreeViewItem::setIcon()
{
    QPixmap icon;

    if ( _orig->isDotEntry() )
    {
	icon = isOpen() ? _view->openDotEntryIcon() : _view->closedDotEntryIcon();
    }
    else if ( _orig->isDir() )
    {
	if ( _orig->readState() == KDirError )
	{
	    icon = _view->unreadableDirIcon();
	    setExpandable( false );
	}
	else
	{
	    icon = isOpen() ? _view->openDirIcon() : _view->closedDirIcon();
	}
    }
    else if ( _orig->isFile() 		)	icon = _view->fileIcon();
    else if ( _orig->isSymLink() 	)	icon = _view->symLinkIcon();
    else if ( _orig->isBlockDevice() 	)	icon = _view->blockDevIcon();
    else if ( _orig->isCharDevice() 	)	icon = _view->charDevIcon();
    else if ( _orig->isSpecial() 	)	icon = _view->fifoIcon();

    setPixmap( _view->iconCol(), icon );
}


void
KDirTreeViewItem::updateSummary()
{
    // Update this item

    setIcon();
    // setText( _view->latestMtimeCol(),	"  " + formatTimeDate( _orig->latestMtime() ) );
    setText( _view->latestMtimeCol(),		"  " + localeTimeDate( _orig->latestMtime() ) );

    if ( _orig->isDir() || _orig->isDotEntry() )
    {
	setText( _view->totalSizeCol(),		" " + formatSize(  _orig->totalSize()	) );
	setText( _view->totalItemsCol(),	" " + formatCount( _orig->totalItems()	) );
	setText( _view->totalFilesCol(),	" " + formatCount( _orig->totalFiles()	) );

	if ( _view->readJobsCol() >= 0 )
	{
	    setText( _view->readJobsCol(),	" " + formatCount( _orig->pendingReadJobs(), true ) );
	}
    }

    if ( _orig->isDir() )
    {
	setText( _view->totalSubDirsCol(),	" " + formatCount( _orig->totalSubDirs() ) );
    }


    // Calculate and display percentage

    if ( _orig->parent() &&				// only if there is a parent as calculation base
	 _orig->parent()->pendingReadJobs() < 1	&&	// not before subtree is finished reading
	 _orig->parent()->totalSize() > 0 )		// avoid division by zero
    {
	 _percent = ( 100.0 * _orig->totalSize() ) / (float) _orig->parent()->totalSize();
	 setText( _view->percentNumCol(), formatPercent ( _percent ) );
    }
    else
    {
	_percent = 0.0;
	setText( _view->percentNumCol(), "" );
    }

    if ( _view->doPacManAnimation() && _orig->isBusy() )
    {
	if ( ! _pacMan )
	    _pacMan = new KPacManAnimation( _view, height()-4, true );

	repaint();
    }


    if ( ! isOpen() )	// Lazy update: Nobody can see the children
	return;		// -> don't update them.


    // Update all children

    KDirTreeViewItem *child = firstChild();

    while ( child )
    {
	child->updateSummary();
	child = child->next();
    }
}


KDirTreeViewItem *
KDirTreeViewItem::locate( KFileInfo *	wanted,
			  bool		lazy,
			  int 		level )
{
    if ( lazy && ! isOpen() )
    {
	/*
	 * When lazy tree cloning is in effect, we don't bother searching all
	 * the children of this item if they are not visible (i.e. the branch
	 * is open) anyway. In this case, cloning that branch is deferred until
	 * the branch is actually opened - which in most cases will never
	 * happen anyway (most users don't manually open each and every
	 * subtree). If and when it happens, we'll probably be fast enough
	 * bringing the view tree in sync with the original tree since opening
	 * a branch requires manual interaction which is a whole lot slower
	 * than copying a couple of objects.
	 */
	return 0;
    }

    if ( _orig == wanted )
    {
	return this;
    }

    if ( level < 0 )
	level = _orig->treeLevel();

    if ( wanted->urlPart( level ) == _orig->name() )
    {
	// Search all children

	KDirTreeViewItem *child = firstChild();

	while ( child )
	{
	    KDirTreeViewItem *foundChild = child->locate( wanted, lazy, level+1 );

	    if ( foundChild )
		return foundChild;
	    else
		child = child->next();
	}
    }

    return 0;
}


void
KDirTreeViewItem::deferredClone()
{
    if ( ! _orig->hasChildren() )
    {
	kdDebug() << k_funcinfo << "Oops, no children - sorry for bothering you!" << endl;
	setExpandable( false );

	return;
    }


    // Clone all normal children

    int level		= _orig->treeLevel();
    bool startingClean	= ! firstChild();
    KFileInfo *origChild = _orig->firstChild();

    while ( origChild )
    {
	if ( startingClean || ! locate( origChild, false, level ) )
	{
	    // kdDebug() << "Deferred cloning " << origChild->debugUrl() << endl;
	    new KDirTreeViewItem( _view, this, origChild );
	}

	origChild = origChild->next();
    }


    // Clone the dot entry

    if ( _orig->dotEntry() &&
	 ( startingClean || ! locate( _orig->dotEntry(), false, level ) ) )
    {
	// kdDebug() << "Deferred cloning dot entry for " << _orig->debugUrl() << endl;
	new KDirTreeViewItem( _view, this, _orig->dotEntry() );
    }
}


void
KDirTreeViewItem::finalizeLocal()
{
    // kdDebug() << k_funcinfo << _orig->debugUrl() << endl;
    cleanupDotEntries();

    if ( _orig->totalItems() == 0 )
	// _orig->hasChildren() would give a wrong answer here since it counts
	// the dot entry, too - which might be removed a moment later.
    {
	setExpandable( false );
    }
}


void
KDirTreeViewItem::cleanupDotEntries()
{
    if ( ! _orig->dotEntry() )
	return;

    KDirTreeViewItem *dotEntry = locate( _orig->dotEntry(), false );

    if ( ! dotEntry )
	return;


    // Reparent dot entry children if there are no subdirectories on this level

    if ( ! _orig->firstChild() )
    {
	// kdDebug() << "Removing solo dot entry clone " << _orig->debugUrl() << endl;
	KDirTreeViewItem *child = dotEntry->firstChild();

	while ( child )
	{
	    KDirTreeViewItem *nextChild = child->next();


	    // Reparent this child

	    // kdDebug() << "Reparenting clone " << child->orig()->debugUrl() << endl;
	    dotEntry->removeItem( child );
	    insertItem( child );

	    child = nextChild;
	}
    }


    // Delete dot entries without any children

    if ( ! _orig->dotEntry()->firstChild() )
    {
	// kdDebug() << "Removing empty dot entry clone " << _orig->debugUrl() << endl;
	delete dotEntry;
    }
}


void
KDirTreeViewItem::setOpen( bool open )
{
    if ( open && _view->doLazyClone() )
	deferredClone();

    QListViewItem::setOpen( open );
    setIcon();

    if ( open )
	updateSummary();
}


QString
KDirTreeViewItem::key ( int column, bool ascending ) const
{
    static QString sortKey;
    NOT_USED( ascending );

    if ( column == _view->totalSizeCol() ||
	 column == _view->percentNumCol()  ||
	 column == _view->percentBarCol()    )
    {
	// Prevent brain-dead compiler warning
	// "ANSI C does not support the `L' length modifier"

	QString format = "%022Ld";
	sortKey.sprintf( format, KFileSizeMax - _orig->totalSize() );
    } else if ( column == _view->ownSizeCol() )
    {
	// Prevent brain-dead compiler warning
	// "ANSI C does not support the `L' length modifier"

	QString format = "%022Ld";
	sortKey.sprintf( format, KFileSizeMax - _orig->size() );
    } else if ( column == _view->totalItemsCol() )
    {
	sortKey.sprintf( "%010d", INT_MAX - _orig->totalItems() );
    } else if ( column == _view->totalFilesCol() )
    {
	sortKey.sprintf( "%010d", INT_MAX - _orig->totalFiles() );
    } else if ( column == _view->totalSubDirsCol() )
    {
	sortKey.sprintf( "%010d", INT_MAX - _orig->totalSubDirs() );
    } else if ( column == _view->readJobsCol() )
    {
	sortKey.sprintf( "%010d", INT_MAX - _orig->pendingReadJobs() );
    } else if ( column == _view->latestMtimeCol() )
    {
	sortKey = formatTimeDate( _orig->latestMtime() );
    } else
    {
	if ( _orig->isDotEntry() )	// make sure this will be the first
	{
	    sortKey.fill( '\001', 20 );
	}
	else
	{
	    sortKey = text( column );
	}
    }

    return sortKey;
}


void
KDirTreeViewItem::paintCell ( QPainter *		painter,
			      const QColorGroup &	colorGroup,
			      int			column,
			      int			width,
			      int			alignment )
{
    if ( column == _view->percentBarCol() )
    {
	if ( _percent > 0.0 )
	{
	    if ( _pacMan )
	    {
		delete _pacMan;
		_pacMan = 0;
	    }
	    
	    painter->setBackgroundColor( colorGroup.base() );
	    int level = _orig->treeLevel();
	    paintPercentageBar ( _percent,
				 painter,
				 _view->treeStepSize() * ( level-1 ),
				 width,
				 _view->fillColor( level-1 ),
				 _view->percentageBarBackground() );
	}
	else
	{
	    if ( _pacMan && _orig->isBusy() )
	    {
		// kdDebug() << "Animating PacMan for " << _orig->debugUrl() << endl;
		// painter->setBackgroundColor( _view->treeBackground() );
		painter->setBackgroundColor( colorGroup.base() );
		_pacMan->animate( painter, QRect( 0, 0, width, height() ) );
	    }
	}
    }
    else
    {
	/*
	 * Call the parent's paintCell() method.  We don't want to do
	 * all the hassle of drawing strings and pixmaps, regarding
	 * alignments etc.
	 */
	QListViewItem::paintCell( painter,
				  colorGroup,
				  column,
				  width,
				  alignment );
    }
}


void
KDirTreeViewItem::paintPercentageBar ( float		percent,
				       QPainter *	painter,
				       int		indent,
				       int		width,
				       const QColor &	fillColor,
				       const QColor &	barBackground )
{
    int penWidth = 2;
    int extraMargin = 3;
    int x = _view->itemMargin();
    int y = extraMargin;
    int w = width    - 2 * _view->itemMargin();
    int h = height() - 2 * extraMargin;
    int fillWidth;

    painter->eraseRect( 0, 0, width, height() );
    w -= indent;
    x += indent;

    if ( w > 0 )
    {
	QPen pen( painter->pen() );
	pen.setWidth( 0 );
	painter->setPen( pen );
	painter->setBrush( NoBrush );
	fillWidth = (int) ( ( w - 2 * penWidth ) * percent / 100.0);


	// Fill bar background.

	painter->fillRect( x + penWidth, y + penWidth,
			   w - 2 * penWidth + 1, h - 2 * penWidth + 1,
			   barBackground );
	/*
	 * Notice: The Xlib XDrawRectangle() function always fills one
	 * pixel less than specified. Altough this is very likely just a
	 * plain old bug, it is documented that way. Obviously, Qt just
	 * maps the fillRect() call directly to XDrawRectangle() so they
	 * inheritit that bug (although the Qt doc stays silent about
	 * it). So it is really necessary to compensate for that missing
	 * pixel in each dimension.
	 *
	 * If you don't believe it, see for yourself.
	 * Hint: Try the xmag program to zoom into the drawn pixels.
	 **/

	// Fill the desired percentage.

	painter->fillRect( x + penWidth, y + penWidth,
			   fillWidth+1, h - 2 * penWidth+1,
			   fillColor );


	// Draw 3D shadows.

	pen.setColor( contrastingColor ( Qt::black,
					 painter->backgroundColor() ) );
	painter->setPen( pen );
	painter->drawLine( x, y, x+w, y );
	painter->drawLine( x, y, x, y+h );

	pen.setColor( contrastingColor( barBackground.dark(),
					painter->backgroundColor() ) );
	painter->setPen( pen );
	painter->drawLine( x+1, y+1, x+w-1, y+1 );
	painter->drawLine( x+1, y+1, x+1, y+h-1 );

	pen.setColor( contrastingColor( barBackground.light(),
					painter->backgroundColor() ) );
	painter->setPen( pen );
	painter->drawLine( x+1, y+h, x+w, y+h );
	painter->drawLine( x+w, y, x+w, y+h );

	pen.setColor( contrastingColor( Qt::white,
					painter->backgroundColor() ) );
	painter->setPen( pen );
	painter->drawLine( x+2, y+h-1, x+w-1, y+h-1 );
	painter->drawLine( x+w-1, y+1, x+w-1, y+h-1 );
    }
}







QString
KDirStat::formatSize( KFileSize lSize )
{
   QString	sizeString;
   double	size;
   QString	unit;

   if ( lSize < 1024 )
   {
      sizeString.setNum( (long) lSize );

      unit = i18n( "Bytes" );
   }
   else
   {
      size = lSize / 1024.0;		// kB

      if ( size < 1024.0 )
      {
	 sizeString.sprintf( "%.1f", size );
	 unit = i18n( "kB" );
      }
      else
      {
	 size /= 1024.0;		// MB

	 if ( size < 1024.0 )
	 {
	    sizeString.sprintf( "%.1f", size );
	    unit = i18n ( "MB" );
	 }
	 else
	 {
	    size /= 1024.0;		// GB - we won't go any further...

	    sizeString.sprintf( "%.2f", size );
	    unit = i18n ( "GB" );
	 }
      }
   }

   if ( ! unit.isEmpty() )
   {
      sizeString += " " + unit;
   }

   return sizeString;
}


QString
KDirStat::formatTime ( long millisec, bool showMilliSeconds )
{
    QString formattedTime;
    int hours;
    int min;
    int sec;

    hours	= millisec / 3600000L;	// 60*60*1000
    millisec	%= 3600000L;

    min		= millisec / 60000L;	// 60*1000
    millisec	%= 60000L;

    sec		= millisec / 1000L;
    millisec	%= 1000L;

    if ( showMilliSeconds )
    {
	formattedTime.sprintf ( "%02d:%02d:%02d.%03ld",
				hours, min, sec, millisec );
    }
    else
    {
	formattedTime.sprintf ( "%02d:%02d:%02d", hours, min, sec );
    }

    return formattedTime;
}


QString
KDirStat::formatCount( int count, bool suppressZero )
{
    if ( suppressZero && count == 0 )
	return "";
    
    QString countString;
    countString.setNum( count );

    return countString;
}


QString
KDirStat::formatPercent( float percent )
{
    QString percentString;

    percentString.sprintf( "%.1f%%", percent );

    return percentString;
}


QString
KDirStat::formatTimeDate( time_t rawTime )
{
    QString timeDateString;
    struct tm *t = localtime( &rawTime );

    /*
     * Format this as "yyyy-mm-dd hh:mm:ss".
     *
     * This format may not be POSIX'ly correct, but it is the ONLY of all those
     * brain-dead formats today's computer users are confronted with that makes
     * any sense to the average human.
     *
     * Agreed, it takes some getting used to, too, but once you got that far,
     * you won't want to miss it.
     *
     * Who the hell came up with those weird formats like described in the
     * ctime() man page? Don't those people ever actually use that?
     *
     * What sense makes a format like "Wed Jun 30 21:49:08 1993" ?
     * The weekday (of all things!) first, then a partial month name, then the
     * day of month, then the time and then - at the very end - the year.
     * IMHO this is maximum brain-dead. Not only can't you do any kind of
     * decent sorting or automatic processing with that disinformation
     * hodge-podge, your brain runs in circles trying to make sense of it.
     *
     * I could put up with crap like that if the Americans and Brits like it
     * that way, but unfortunately I as a German am confronted with that
     * bullshit, too, on a daily basis - either because some localization stuff
     * didn't work out right (again) or because some jerk decided to emulate
     * this stuff in the German translation, too. I am sick and tired with
     * that, and since this is MY program I am going to use a format that makes
     * sense to ME.
     *
     * No, no exceptions for Americans or Brits. I had to put up with their
     * crap long enough, now it's time for them to put up with mine.
     * Payback time - though luck, folks.
     * ;-)
     *
     * Stefan Hundhammer <sh@suse.de>	2001-05-28
     * (in quite some fit of frustration)
     */
    timeDateString.sprintf( "%4d-%02d-%02d  %02d:%02d:%02d",
			    t->tm_year + 1900,
			    t->tm_mon  + 1,	// another brain-dead common pitfall - 0..11
			    t->tm_mday,
			    t->tm_hour, t->tm_min, t->tm_sec );

    return timeDateString;
}



QString
KDirStat::localeTimeDate( time_t rawTime )
{
    QDateTime timeDate;
    timeDate.setTime_t( rawTime );
    QString timeDateString =
	KGlobal::locale()->formatDate( timeDate.date(), true ) + "  " +	// short format
	KGlobal::locale()->formatTime( timeDate.time(), true );		// include seconds

    return timeDateString;
}


QColor
KDirStat::contrastingColor ( const QColor &desiredColor,
			     const QColor &contrastColor )
{
    if ( desiredColor != contrastColor )
    {
	return desiredColor;
    }

    if ( contrastColor != contrastColor.light() )
    {
	// try a little lighter
	return contrastColor.light();
    }
    else
    {
	// try a little darker
	return contrastColor.dark();
    }
}





// EOF