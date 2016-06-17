#include <bdn/init.h>
#include <bdn/LayoutCoordinator.h>


namespace bdn
{

LayoutCoordinator::LayoutCoordinator()
{
}

void LayoutCoordinator::viewNeedsSizingInfoUpdate(View* pView)
{
	MutexLock lock( _mutex );

	_sizingInfoSet.add( pView );

	needUpdate();
}

void LayoutCoordinator::viewNeedsLayout(View* pView)
{
	MutexLock lock( _mutex );

	_layoutSet.add( pView );

	needUpdate();
}


void LayoutCoordinator::updateNow()
{
	if(Thread::isCurrentMain())
		mainThreadUpdateNow();
	else
	{
		// if we have anything to update then an update will already be scheduled.
		// There is no need to schedule another one now. So do nothing in this case.
	}
}


void LayoutCoordinator::needUpdate()
{
	if(!_updateScheduled)
	{
		P<LayoutCoordinator> pThis = this;

		_updateScheduled = true;

		// note that we use asyncCallFromMainThread here. I.e. if we are
		// in the main thread then this is still scheduled for later, rather
		// than run immediately.
		// That is what we want, because that allows us to collect and combine
		// multiple operations.
		asyncCallFromMainThread(
			[pThis]()
			{
				{
					MutexLock lock( _mutex );
					pThis->_updateScheduled = false;
				}

				pThis->mainThreadUpdateNow();
			) );
	}
}


	


void LayoutCoordinator::mainThreadUpdateNow()
{
	// first we have to determine the order in which we should
	// update the views.
	// For resizing the optimal order is child-to-parent. Because if
	// a child's child changes then this might influence the parent's size.
	// If we would update the parent first and then the child, then the child update
	// might cause the parent to need another update afterwards.

	struct ToDo
	{
		ToDo(const P<View>& pView)
		{
			this->pView = pView;

			// find out the view's level inside the UI tree
			_level = 0;
			P<View> pCurrParent = pView->getParentView();
			while(pCurrParent!=nullptr)
			{
				_level++;
				pCurrParent = pCurrParent->getParentView();
			)
		}

		bool operator<(const ToDo& o) const
		{
			return (level<o.level || (level==o.level && pView.getPtr() < o.pView.getPtr() );
		}

		P<View> pView;
		int		level;
	};


	// note that this loop is structured in a way so that new objects can be added during
	// the sizing info update of each view.
	std::vector< ToDo > toDoList;
	while(true)
	{
		std::set< P<View> > newlyAddedSet;

		{
			MutexLock lock( _mutex );
			newlyAddedSet = _sizingInfoSet;
			_sizingInfoSet.clear();
		}
				
		// add the new requests to the todo list.
		// Keep the global UI mutex locked during this, because we do not
		// want any changes made to the view hierarchy during this (since we compute
		// our level value based on the hierarchy)
		if(!newlyAddedSet.empty())
		{
			{
				MutexLock lock( getGlobalUiMutex() );

				toDoList.push_back( toDoList.end(), newlyAddedSet.begin(), newlyAddedSet.end() );
			}

			// re-sort the list. We want inverted order: higher levels (=children) first.
			// Note that we accept that if any changes are made to the UI hierarchy during this
			// then we will have an unoptimal order. But that case should be very rare, 
			// and the end result will still be correct. So we ignore it.
			std::sort(	toDoList.begin(),
						toDoList.end(),
						[](const ToDo& a, const ToDo& b)
						{
							// invert the order. We want higher levels (=children) first
							return !(a<b);
						} );
		}		

		if(toDoList.empty())
		{
			// done.
			break;
		}

		ToDo toDo = toDoList.front();
		toDoList.pop_front();

		toDo.pView->mainThreadUpdateSizingInfo();		
	}

		
	
	// now do the same for the layout operations.
	// Note that layout operations that have been triggered by any
	// of the resizing are included in this!
	std::vector< ToDo > toDoList;
	while(true)
	{
		std::set< P<View> > newlyAddedSet;

		{
			MutexLock lock( _mutex );
			newlyAddedSet = _sizingInfoSet;
			_sizingInfoSet.clear();
		}
				
		// add the new requests to the todo list.
		// Keep the global UI mutex locked during this, because we do not
		// want any changes made to the view hierarchy during this (since we compute
		// our level value based on the hierarchy)
		if(!newlyAddedSet.empty())
		{
			{
				MutexLock lock( getGlobalUiMutex() );

				toDoList.push_back( toDoList.end(), newlyAddedSet.begin(), newlyAddedSet.end() );
			}

			// re-sort the list
			// use the normal order: smaller levels (=parents) first.
			// Note that we accept that if any changes are made to the UI hierarchy during this
			// then we will have an unoptimal order. But that case should be very rare, 
			// and the end result will still be correct. So we ignore it.
			std::sort(	toDoList.begin(), toDoList.end() );
		}


		if(toDoList.empty())
		{
			// done.
			break;
		}	

		
		ToDo toDo = toDoList.front();
		toDoList.pop_front();

		toDo.pView->mainThreadLayout();
	}	
}

