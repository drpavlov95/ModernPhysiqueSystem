class FileList extends skyui.components.list.ScrollingList
{
   var _entryList;
   var _listIndex;
   var _maxScrollPosition;
   var _selectedIndex;
   var disableSelection;
   var doSetSelectedIndex;
   var getListEnumRelativeIndex;
   var getListEnumSize;
   var getSelectedListEnumIndex;
   var isMouseDrivenNav;
   var scrollDelta;
   var scrollPosition;
   var selectDefaultIndex;
   var entryHeight = 25;
   var isAutoUnselect = true;
   function FileList()
   {
      super();
   }
   function set entryList(a_list)
   {
      this._entryList = a_list;
   }
   function moveSelectionUp(a_bScrollPage)
   {
      var _loc2_;
      if(!this.disableSelection && !a_bScrollPage)
      {
         if(this._selectedIndex == -1)
         {
            this.selectDefaultIndex(false);
         }
         else if(this.getSelectedListEnumIndex() >= this.scrollDelta)
         {
            this.doSetSelectedIndex(this.getListEnumRelativeIndex(- this.scrollDelta),skyui.components.list.BasicList.SELECT_KEYBOARD);
            this.isMouseDrivenNav = false;
         }
         else
         {
            this.doSetSelectedIndex(-1,skyui.components.list.BasicList.SELECT_MOUSE);
         }
      }
      else if(a_bScrollPage)
      {
         _loc2_ = this.scrollPosition - this._listIndex;
         this.scrollPosition = _loc2_ <= 0 ? 0 : _loc2_;
         this.doSetSelectedIndex(-1,skyui.components.list.BasicList.SELECT_MOUSE);
      }
      else
      {
         this.scrollPosition -= this.scrollDelta;
      }
   }
   function moveSelectionDown(a_bScrollPage)
   {
      var _loc2_;
      if(!this.disableSelection && !a_bScrollPage)
      {
         if(this._selectedIndex == -1)
         {
            this.selectDefaultIndex(true);
         }
         else if(this.getSelectedListEnumIndex() < this.getListEnumSize() - this.scrollDelta)
         {
            this.doSetSelectedIndex(this.getListEnumRelativeIndex(this.scrollDelta),skyui.components.list.BasicList.SELECT_KEYBOARD);
            this.isMouseDrivenNav = false;
         }
         else
         {
            this.doSetSelectedIndex(-1,skyui.components.list.BasicList.SELECT_MOUSE);
         }
      }
      else if(a_bScrollPage)
      {
         _loc2_ = this.scrollPosition + this._listIndex;
         this.scrollPosition = _loc2_ >= this._maxScrollPosition ? this._maxScrollPosition : _loc2_;
         this.doSetSelectedIndex(-1,skyui.components.list.BasicList.SELECT_MOUSE);
      }
      else
      {
         this.scrollPosition += this.scrollDelta;
      }
   }
}
