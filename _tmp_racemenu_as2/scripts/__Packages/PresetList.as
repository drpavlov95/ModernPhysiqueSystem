class PresetList extends skyui.components.list.ScrollingList
{
   var _selectedIndex;
   var disableSelection;
   var getListEnumRelativeIndex;
   var getListEnumSize;
   var getSelectedListEnumIndex;
   var isMouseDrivenNav;
   var entryHeight = 25;
   function PresetList()
   {
      super();
   }
   function invalidateSelection()
   {
      this.doSetSelectedIndex(-1,skyui.components.list.BasicList.SELECT_MOUSE);
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
