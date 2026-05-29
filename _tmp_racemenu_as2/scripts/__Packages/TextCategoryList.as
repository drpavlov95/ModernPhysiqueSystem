class TextCategoryList extends skyui.components.list.BasicList
{
   var _entryClipManager;
   var _entryList;
   var _selectedIndex;
   var dispatchEvent;
   var listEnumeration;
   var listState;
   var onInvalidate;
   var selectedEntry;
   var _listIndex = 0;
   var _curClipIndex = -1;
   var _clipSpacing = 10;
   function TextCategoryList()
   {
      super();
   }
   function clearList()
   {
      this._entryList.splice(0);
   }
   function InvalidateData()
   {
      if(this._bSuspended)
      {
         this._bRequestInvalidate = true;
         return undefined;
      }
      var _loc2_ = 0;
      while(_loc2_ < this._entryList.length)
      {
         this._entryList[_loc2_].itemIndex = _loc2_;
         this._entryList[_loc2_].clipIndex = undefined;
         _loc2_ = _loc2_ + 1;
      }
      this.listEnumeration.invalidate();
      if(this._selectedIndex >= this.listEnumeration.size())
      {
         this._selectedIndex = this.listEnumeration.size() - 1;
      }
      this.UpdateList();
      var _loc3_;
      if(this._curClipIndex != undefined && this._curClipIndex != -1 && this._listIndex > 0)
      {
         if(this._curClipIndex >= this._listIndex)
         {
            this._curClipIndex = this._listIndex - 1;
         }
         _loc3_ = this.getClipByIndex(this._curClipIndex);
         this.doSetSelectedIndex(_loc3_.itemIndex,skyui.components.list.BasicList.SELECT_MOUSE);
      }
      if(this.onInvalidate)
      {
         this.onInvalidate();
      }
   }
   function UpdateList()
   {
      if(this._bSuspended)
      {
         this._bRequestUpdate = true;
         return undefined;
      }
      this.setClipCount(this._entryList.length);
      var _loc3_ = 0;
      while(_loc3_ < this.getListEnumSize())
      {
         this.getListEnumEntry(_loc3_).clipIndex = undefined;
         _loc3_ = _loc3_ + 1;
      }
      this._listIndex = 0;
      _loc3_ = 0;
      var _loc4_;
      var _loc2_;
      while(_loc3_ < this._entryList.length)
      {
         _loc4_ = this.getClipByIndex(this._listIndex);
         _loc2_ = this.getListEnumEntry(_loc3_);
         _loc4_.setEntry(_loc2_,this.listState);
         _loc4_.itemIndex = _loc2_.itemIndex;
         _loc2_.clipIndex = this._listIndex;
         this._listIndex = this._listIndex + 1;
         _loc3_ = _loc3_ + 1;
      }
      var _loc5_ = 0;
      _loc3_ = 0;
      while(_loc3_ < this._entryList.length)
      {
         _loc4_ = this.getClipByIndex(_loc3_);
         _loc4_._x = _loc5_;
         _loc5_ += _loc4_.background._width + this._clipSpacing;
         _loc4_._visible = true;
         _loc3_ = _loc3_ + 1;
      }
   }
   function gotoStart()
   {
      if(this.disableSelection)
      {
         return undefined;
      }
      var _loc2_ = this._entryList.length - 1;
      var _loc4_ = this._curClipIndex;
      var _loc3_;
      do
      {
         if(_loc2_ < this._entryList.length - 1)
         {
            _loc2_ = _loc2_ + 1;
         }
         else
         {
            _loc2_ = 0;
         }
         _loc3_ = this.getClipByIndex(_loc2_);
      }
      while(_loc2_ != _loc4_ && _loc3_.filterFlag == 0 && !_loc3_.bDontHide);
      this.onItemPress(_loc3_.itemIndex,0);
   }
   function gotoEnd()
   {
      if(this.disableSelection)
      {
         return undefined;
      }
      var _loc2_ = 0;
      var _loc4_ = this._curClipIndex;
      var _loc3_;
      do
      {
         if(_loc2_ > 0)
         {
            _loc2_ = _loc2_ - 1;
         }
         else
         {
            _loc2_ = this._entryList.length - 1;
         }
         _loc3_ = this.getClipByIndex(_loc2_);
      }
      while(_loc2_ != _loc4_ && _loc3_.filterFlag == 0 && !_loc3_.bDontHide);
      this.onItemPress(_loc3_.itemIndex,0);
   }
   function moveSelectionLeft(a_wrap)
   {
      if(this.disableSelection)
      {
         return undefined;
      }
      var _loc2_ = this._curClipIndex;
      var _loc4_ = this._curClipIndex;
      var _loc3_;
      do
      {
         if(_loc2_ > 0)
         {
            _loc2_ = _loc2_ - 1;
         }
         else if(a_wrap)
         {
            _loc2_ = this._entryList.length - 1;
         }
         _loc3_ = this.getClipByIndex(_loc2_);
      }
      while(_loc2_ != _loc4_ && _loc3_.filterFlag == 0 && !_loc3_.bDontHide);
      this.onItemPress(_loc3_.itemIndex,0);
   }
   function moveSelectionRight(a_wrap)
   {
      if(this.disableSelection)
      {
         return undefined;
      }
      var _loc2_ = this._curClipIndex;
      var _loc4_ = this._curClipIndex;
      var _loc3_;
      do
      {
         if(_loc2_ < this._entryList.length - 1)
         {
            _loc2_ = _loc2_ + 1;
         }
         else if(a_wrap)
         {
            _loc2_ = 0;
         }
         _loc3_ = this.getClipByIndex(_loc2_);
      }
      while(_loc2_ != _loc4_ && _loc3_.filterFlag == 0 && !_loc3_.bDontHide);
      this.onItemPress(_loc3_.itemIndex,0);
   }
   function getClipData()
   {
      return [this.selectedClip._x,this.selectedClip.background._width];
   }
   function handleInput(details, pathToFocus)
   {
      if(this.disableInput)
      {
         return false;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.LEFT)
         {
            this.moveSelectionLeft(true);
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.RIGHT)
         {
            this.moveSelectionRight(true);
            return true;
         }
      }
      return false;
   }
   function doSetSelectedIndex(a_newIndex, a_keyboardOrMouse)
   {
      if(this.disableSelection || a_newIndex == this._selectedIndex)
      {
         return undefined;
      }
      if(a_newIndex != -1 && this.getListEnumIndex(a_newIndex) == undefined)
      {
         return undefined;
      }
      var _loc2_ = this._selectedIndex;
      this._selectedIndex = a_newIndex;
      var _loc4_;
      if(_loc2_ != -1)
      {
         _loc4_ = this._entryClipManager.getClip(this._entryList[_loc2_].clipIndex);
         _loc4_.setEntry(this._entryList[_loc2_],this.listState);
      }
      if(this._selectedIndex != -1)
      {
         _loc4_ = this._entryClipManager.getClip(this._entryList[this._selectedIndex].clipIndex);
         _loc4_.setEntry(this._entryList[this._selectedIndex],this.listState);
      }
      if(this._selectedIndex != -1)
      {
         this._curClipIndex = this._entryList[this._selectedIndex].clipIndex;
      }
      else
      {
         this._curClipIndex = -1;
      }
      this.dispatchEvent({type:"selectionChange",index:this._selectedIndex,keyboardOrMouse:a_keyboardOrMouse});
   }
   function onItemPress(a_index, a_keyboardOrMouse)
   {
      if(this.disableInput || this.disableSelection || a_index == -1)
      {
         return undefined;
      }
      this.doSetSelectedIndex(a_index,a_keyboardOrMouse);
      this.dispatchEvent({type:"itemPress",index:this._selectedIndex,entry:this.selectedEntry,keyboardOrMouse:a_keyboardOrMouse});
   }
   function onItemPressAux(a_index, a_keyboardOrMouse, a_buttonIndex)
   {
      if(this.disableInput || this.disableSelection || a_index == -1 || a_buttonIndex != 1)
      {
         return undefined;
      }
      this.doSetSelectedIndex(a_index,a_keyboardOrMouse);
      this.dispatchEvent({type:"itemPressAux",index:this._selectedIndex,entry:this.selectedEntry,keyboardOrMouse:a_keyboardOrMouse});
   }
   function onItemRollOver(a_index)
   {
      if(this.disableInput || this.disableSelection)
      {
         return undefined;
      }
      this.isMouseDrivenNav = true;
      if(a_index == this._selectedIndex)
      {
         return undefined;
      }
      var _loc2_ = this.getClipByIndex(this._entryList[a_index].clipIndex);
      _loc2_._alpha = 75;
   }
   function onItemRollOut(a_index)
   {
      if(this.disableInput || this.disableSelection)
      {
         return undefined;
      }
      this.isMouseDrivenNav = true;
      if(a_index == this._selectedIndex)
      {
         return undefined;
      }
      var _loc2_ = this.getClipByIndex(this._entryList[a_index].clipIndex);
      _loc2_._alpha = 50;
   }
}
