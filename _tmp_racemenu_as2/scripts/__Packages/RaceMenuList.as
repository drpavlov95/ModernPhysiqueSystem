class RaceMenuList extends skyui.components.list.ScrollingList
{
   var _curClipIndex;
   var _listHeight;
   var _listIndex;
   var _maxListIndex;
   var _maxScrollPosition;
   var _platform;
   var _selectedIndex;
   var _textureControl;
   var background;
   var disableInput;
   var disableSelection;
   var dispatchEvent;
   var doSetSelectedIndex;
   var getClipByIndex;
   var getListEnumRelativeIndex;
   var getListEnumSize;
   var getSelectedListEnumIndex;
   var isMouseDrivenNav;
   var onItemPress;
   var scrollDelta;
   var scrollPosition;
   var scrollbar;
   var selectDefaultIndex;
   var selectedClip;
   var selectedEntry;
   var entryHeight = 25;
   function RaceMenuList()
   {
      super();
   }
   function set listHeight(a_height)
   {
      this._listHeight = this.background._height = a_height;
      if(this.scrollbar != undefined)
      {
         this.scrollbar.height = this._listHeight;
      }
      this._maxListIndex = Math.floor(this._listHeight / this.entryHeight);
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      super.setPlatform(a_platform,a_bPS3Switch);
      this._platform = a_platform;
      if(this._platform == 0)
      {
         this._textureControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Wait",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._textureControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Jump",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      if(this.disableInput)
      {
         return false;
      }
      var _loc3_ = this.selectedClip != undefined && this.selectedClip.handleInput != undefined && this.selectedClip.handleInput(details,pathToFocus.slice(1));
      if(_loc3_)
      {
         return true;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.UP || details.navEquivalent == gfx.ui.NavigationCode.PAGE_UP || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_L3)
         {
            this.moveSelectionUp(details.navEquivalent == gfx.ui.NavigationCode.PAGE_UP || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_L3);
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.DOWN || details.navEquivalent == gfx.ui.NavigationCode.PAGE_DOWN || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_R3)
         {
            this.moveSelectionDown(details.navEquivalent == gfx.ui.NavigationCode.PAGE_DOWN || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_R3);
            return true;
         }
         if(!this.disableSelection && (details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Activate",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0)))
         {
            this.onItemPress();
            return true;
         }
         if(!this.disableSelection && this.IsBoundKeyPressed(details,this._textureControl,this._platform))
         {
            this.dispatchEvent({type:"itemPressSecondary",index:this._selectedIndex,entry:this.selectedEntry,clip:this.selectedClip});
            return true;
         }
      }
      return false;
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
   function getClipGlobalCoordinate()
   {
      var _loc5_ = this.getClipByIndex(this._curClipIndex);
      var _loc4_ = _loc5_._x;
      var _loc3_ = _loc5_._y + this._curClipIndex * 0.95;
      var _loc2_ = _loc5_._parent;
      while(_loc2_)
      {
         _loc4_ += _loc2_._x;
         _loc3_ += _loc2_._y;
         _loc2_ = _loc2_._parent;
      }
      return {x:_loc4_,y:_loc3_};
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
