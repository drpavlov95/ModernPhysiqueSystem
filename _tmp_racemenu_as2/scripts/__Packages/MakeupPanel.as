class MakeupPanel extends MovieClip
{
   var _acceptButton;
   var _cancelButton;
   var _currentDisplayText;
   var _currentTexture;
   var _nameFilter;
   var _platform;
   var _sortFilter;
   var buttonPanel;
   var dispatchEvent;
   var entry;
   var itemCount;
   var makeupList;
   var searchWidget;
   var bTextEntryMode = false;
   var _initParams = null;
   function MakeupPanel()
   {
      super();
      this._sortFilter = new skyui.filter.SortFilter();
      this._nameFilter = new skyui.filter.NameFilter();
      Shared.GlobalFunc.MaintainTextFormat();
      Shared.GlobalFunc.SetLockFunction();
      gfx.events.EventDispatcher.initialize(this);
   }
   function onLoad()
   {
      super.onLoad();
      var _loc3_ = new skyui.components.list.FilteredEnumeration(this.makeupList.entryList);
      _loc3_.addFilter(this._sortFilter);
      _loc3_.addFilter(this._nameFilter);
      this.makeupList.listEnumeration = _loc3_;
      this.makeupList.onInvalidate = function()
      {
         this._parent.textureCount.text = this.itemCount.toString();
      };
      this._sortFilter.setSortBy(["text"],[0]);
      this._sortFilter.addEventListener("filterChange",this,"onFilterChange");
      this._nameFilter.addEventListener("filterChange",this,"onFilterChange");
      this.makeupList.addEventListener("itemPress",this,"onItemPress");
      this.makeupList.addEventListener("selectionChange",this,"onSelectionChanged");
      this.searchWidget.addEventListener("inputStart",this,"onSearchInputStart");
      this.searchWidget.addEventListener("inputEnd",this,"onSearchInputEnd");
      this.searchWidget.addEventListener("inputChange",this,"onSearchInputChange");
      this.searchWidget.gotoAndStop("Double");
   }
   function set initParams(a_object)
   {
      for(var _loc3_ in a_object)
      {
         this[_loc3_] = a_object[_loc3_];
      }
   }
   function onSearchInputStart(event)
   {
      this.bTextEntryMode = true;
      this.makeupList.disableSelection = this.makeupList.disableInput = true;
      this._nameFilter.filterText = "";
   }
   function onSearchInputChange(event)
   {
      this._nameFilter.filterText = event.data;
   }
   function onSearchInputEnd(event)
   {
      this.makeupList.disableSelection = this.makeupList.disableInput = false;
      this._nameFilter.filterText = event.data;
      this.bTextEntryMode = false;
   }
   function onFilterChange()
   {
      this.makeupList.InvalidateData();
   }
   function clearFilter()
   {
      this._nameFilter.filterText = "";
      this.searchWidget.endInput();
   }
   function InvalidateList()
   {
      this.makeupList.InvalidateData();
   }
   function setMakeupList(list)
   {
      if(this.makeupList.listEnumeration)
      {
         delete this.makeupList.listEnumeration;
         this.makeupList.listEnumeration = null;
      }
      this.makeupList.entryList = list;
      var _loc2_;
      if(this.makeupList.entryList)
      {
         _loc2_ = new skyui.components.list.FilteredEnumeration(this.makeupList.entryList);
         _loc2_.addFilter(this._sortFilter);
         _loc2_.addFilter(this._nameFilter);
         this.makeupList.listEnumeration = _loc2_;
         this.makeupList.InvalidateData();
      }
   }
   function handleInput(details, pathToFocus)
   {
      var _loc2_ = false;
      if(Shared.GlobalFunc.IsKeyPressed(details) && !this.bTextEntryMode)
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Activate",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onAccept();
            _loc2_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.TAB || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Cancel",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onCancel();
            _loc2_ = true;
         }
      }
      var _loc4_;
      if(!_loc2_)
      {
         _loc4_ = pathToFocus.shift();
         _loc2_ = _loc4_.handleInput(details,pathToFocus);
      }
      return _loc2_;
   }
   function SetupButtons()
   {
      this.buttonPanel.clearButtons();
      var _loc2_ = this.buttonPanel.addButton({text:"$Accept",controls:this._acceptButton});
      var _loc3_ = this.buttonPanel.addButton({text:"$Cancel",controls:this._cancelButton});
      _loc2_.addEventListener("click",this,"onAccept");
      _loc3_.addEventListener("click",this,"onCancel");
      this.buttonPanel.updateButtons(true);
   }
   function updateButtons(bInstant)
   {
      this.buttonPanel.updateButtons(bInstant);
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      if(a_platform == 0)
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = {name:"Tween Menu",context:skyui.defines.Input.CONTEXT_GAMEPLAY};
      }
      else
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = skyui.defines.Input.Cancel;
      }
      this.buttonPanel.setPlatform(a_platform,a_bPS3Switch);
   }
   function setSelectedEntry(a_texture)
   {
      var _loc2_ = 0;
      while(_loc2_ < this.makeupList.entryList.length)
      {
         if(this.makeupList.entryList[_loc2_].texture == a_texture)
         {
            this.makeupList.selectedIndex = _loc2_;
            break;
         }
         _loc2_ = _loc2_ + 1;
      }
   }
   function setTexture(a_displayText, a_texture)
   {
      this._currentDisplayText = a_displayText;
      this._currentTexture = a_texture;
   }
   function onItemPress(event)
   {
      var _loc2_ = this.makeupList.entryList[event.index];
      if(_loc2_)
      {
         this.dispatchEvent({type:"changeTexture",entry:this.entry,texture:_loc2_.texture,displayText:_loc2_.displayText,apply:true});
      }
   }
   function onAccept()
   {
      var _loc2_ = this.makeupList.listState.selectedEntry;
      if(_loc2_)
      {
         this.dispatchEvent({type:"changeTexture",entry:this.entry,texture:_loc2_.texture,displayText:_loc2_.displayText,apply:true});
      }
   }
   function onCancel()
   {
      this.dispatchEvent({type:"changeTexture",entry:this.entry,texture:this._currentTexture,displayText:this._currentDisplayText,apply:true});
   }
   function onSelectionChanged(event)
   {
      this.makeupList.listState.selectedEntry = this.makeupList.entryList[event.index];
      if(this.makeupList.listState.selectedEntry)
      {
         this.dispatchEvent({type:"changeTexture",entry:this.entry,texture:this.makeupList.listState.selectedEntry.texture,displayText:this.makeupList.listState.selectedEntry.displayText,apply:false});
      }
      gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
   }
}
