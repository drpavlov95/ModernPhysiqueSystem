class RaceMenu extends MovieClip
{
   var _acceptControl;
   var _activateControl;
   var _bPS3Switch;
   var _categorySortFilter;
   var _itemSortFilter;
   var _lightControl;
   var _nameFilter;
   var _panelX;
   var _platform;
   var _raceList;
   var _reloadInterval;
   var _savedColor;
   var _searchControl;
   var _sliderInitializeInterval;
   var _sortFilter;
   var _textureControl;
   var _typeFilter;
   var _updateInterval;
   var _zoomControl;
   var bLimitedMenu;
   var bonusList;
   var bonusPanel;
   var bottomBar;
   var cameraEditor;
   var categoryButtons;
   var categoryList;
   var colorField;
   var customSliders;
   var entryObject;
   var fillColor;
   var itemDescriptor;
   var itemList;
   var listType;
   var loadingIcon;
   var makeupList;
   var makeupPanel;
   var modeSelect;
   var navPanel;
   var position;
   var presetEditor;
   var raceDescription;
   var racePanel;
   var searchWidget;
   var sliderID;
   var textDisplay;
   var textEntry;
   var tintType;
   var vertexEditor;
   static var RACEMENU_VERSION_MAJOR = 3;
   static var RACEMENU_VERSION_MINOR = 4;
   static var RACEMENU_VERSION_RELEASE = 5;
   static var RACEMENU_VERSION_BETA = 0;
   static var RACEMENU_VERSION_IDX = 8;
   var _pendingData = null;
   var _pauseInputHandling = false;
   var ITEMLIST_HEIGHT_FULL = 528;
   var ITEMLIST_HEIGHT_SHARED = 335;
   var ITEMLIST_HIDDEN_X = -478;
   var DESCRIPTION_WIDTH_FULL = 405;
   var DESCRIPTION_WIDTH_SHARED = 262;
   var BOTTOMBAR_SHOWN_Y = 645;
   var BOTTOMBAR_HIDDEN_Y = 745;
   var bPlayerZoom = true;
   var bShowLight = true;
   var bTextEntryMode = false;
   var bMenuInitialized = false;
   var bRaceChanging = false;
   var bCustomMenu = true;
   var bPhysiqueBodyCategoryActive = false;
   var bPhysiqueSlidersModeActive = true;
   var bPhysiqueWeightSyncRebuilding = false;
   var physiqueTitle = null;
   var physiqueWeightSyncInterval = null;
   var physiqueWeightSyncResumeInterval = null;
   var statusText = "";
   function RaceMenu()
   {
      super();
      com.greensock.plugins.TweenPlugin.activate([com.greensock.plugins.AutoAlphaPlugin]);
      PapyrusInterface.initialize(this);
      _global.tintCount = 0;
      _global.maxTints = RaceMenuDefines.MAX_TINTS;
      _global.eventPrefix = "RSM_";
      _global.presetSlot = 0;
      this.itemList = this.racePanel.itemList;
      this.categoryList = this.racePanel.slidingCategoryList.categoryList;
      this.categoryButtons = this.racePanel.categoryButtons;
      this.searchWidget = this.racePanel.searchWidget;
      this.navPanel = this.bottomBar.buttonPanel;
      this.bonusList = this.raceDescription.bonusList;
      this.bonusPanel = this.bonusList;
      this.customSliders = new Array();
      this._raceList = new Array();
      this.makeupList = new Array();
      this.makeupList.push(new Array());
      this.makeupList.push(new Array());
      this.makeupList.push(new Array());
      this.makeupList.push(new Array());
      this.makeupList.push(new Array());
      this.textDisplay._alpha = 0;
      this.textDisplay._visible = this.textDisplay.enabled = false;
      this.loadingIcon._visible = this.loadingIcon.enabled = false;
      this.textEntry._visible = this.textEntry.enabled = false;
      this.itemDescriptor._visible = this.itemDescriptor.enabled = false;
      this.vertexEditor._visible = this.vertexEditor.enabled = false;
      this.cameraEditor._visible = this.cameraEditor.enabled = false;
      this.presetEditor._visible = this.presetEditor.enabled = false;
      Shared.GlobalFunc.MaintainTextFormat();
      Shared.GlobalFunc.SetLockFunction();
      gfx.events.EventDispatcher.initialize(this);
      this._typeFilter = new CategoryFilter();
      this._nameFilter = new skyui.filter.NameFilter();
      this._sortFilter = new skyui.filter.SortFilter();
      this._categorySortFilter = new skyui.filter.SortFilter();
      this._itemSortFilter = new skyui.filter.SortFilter();
      gfx.io.GameDelegate.initialize();
      gfx.io.GameDelegate.addCallBack("SetCategoriesList",this,"SetCategoriesList");
      gfx.io.GameDelegate.addCallBack("ShowTextEntry",this,"ShowTextEntry");
      gfx.io.GameDelegate.addCallBack("SetNameText",this,"SetNameText");
      gfx.io.GameDelegate.addCallBack("SetRaceText",this,"SetRaceText");
      gfx.io.GameDelegate.addCallBack("SetRaceList",this,"SetRaceList");
      gfx.io.GameDelegate.addCallBack("SetOptionSliders",this,"SetSliders");
      gfx.io.GameDelegate.addCallBack("ShowTextEntryField",this,"ShowTextEntryField");
      gfx.io.GameDelegate.addCallBack("HideLoadingIcon",this,"HideLoadingIcon");
   }
   function onLoad()
   {
      super.onLoad();
      this.ShowRaceBonuses(null,false);
      this.ShowRaceDescription(false);
      this.ShowColorField(false);
      this.ShowMakeupPanel(false);
      this.raceDescription.textField.textAutoSize = "shrink";
      var _loc3_ = new skyui.components.list.FilteredEnumeration(this.bonusList.entryList);
      _loc3_.addFilter(this._sortFilter);
      this.bonusList.listEnumeration = _loc3_;
      var _loc4_ = new skyui.components.list.FilteredEnumeration(this.categoryList.entryList);
      _loc4_.addFilter(this._categorySortFilter);
      this.categoryList.listEnumeration = _loc4_;
      var _loc5_ = new skyui.components.list.FilteredEnumeration(this.itemList.entryList);
      _loc5_.addFilter(this._typeFilter);
      _loc5_.addFilter(this._nameFilter);
      _loc5_.addFilter(this._itemSortFilter);
      this.itemList.listEnumeration = _loc5_;
      this._sortFilter.addEventListener("filterChange",this,"onBonusFilterChange");
      this._typeFilter.addEventListener("filterChange",this,"onFilterChange");
      this._nameFilter.addEventListener("filterChange",this,"onFilterChange");
      this._itemSortFilter.addEventListener("filterChange",this,"onFilterChange");
      this._categorySortFilter.addEventListener("filterChange",this,"onCategoryFilterChange");
      this.itemList.addEventListener("itemPress",this,"onItemPress");
      this.itemList.addEventListener("itemPressSecondary",this,"onItemPressSecondary");
      this.itemList.addEventListener("itemPressAux",this,"onItemPressAux");
      this.itemList.addEventListener("selectionChange",this,"onSelectionChange");
      this.categoryList.addEventListener("itemPress",this,"onCategoryPress");
      this.categoryList.addEventListener("selectionChange",this,"onCategoryChange");
      this.categoryButtons.addEventListener("pressLeft",this.categoryList,"gotoStart");
      this.categoryButtons.addEventListener("pressRight",this.categoryList,"gotoEnd");
      this.searchWidget.addEventListener("inputStart",this,"onSearchInputStart");
      this.searchWidget.addEventListener("inputEnd",this,"onSearchInputEnd");
      this.searchWidget.addEventListener("inputChange",this,"onSearchInputChange");
      this.textEntry.addEventListener("nameChange",this,"onNameChange");
      this.colorField.addEventListener("changeColor",this,"onChangeColor");
      this.colorField.addEventListener("switchMode",this,"onSwitchColorMode");
      this.colorField.addEventListener("saveColor",this,"onSaveColor");
      this.makeupPanel.addEventListener("changeTexture",this,"onChangeTexture");
      this.modeSelect.addEventListener("changeMode",this,"onChangeMode");
      this.modeSelect.addEventListener("tabRollOver",this,"onTabRollOver");
      this.presetEditor.addEventListener("loadedPreset",this,"onLoadPreset");
      this.presetEditor.addEventListener("savedPreset",this,"onSavePreset");
      this.vertexEditor.addEventListener("importedFile",this,"onImportedFile");
      this._sortFilter.setSortBy(["text"],[0]);
      this._categorySortFilter.setSortBy(["priority","itemIndex"],[Array.NUMERIC,Array.NUMERIC]);
      this._itemSortFilter.setSortBy(["priority","itemIndex"],[Array.NUMERIC,Array.NUMERIC]);
      this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:true,filterFlag:1,text:"$ALL",flag:2044,savedItemIndex:-1,priority:RaceMenuDefines.CATEGORY_PRIORITY_START,enabled:true});
      this.categoryList.InvalidateData();
      this.categoryList.onItemPress(0,0);
      this.itemList.requestInvalidate();
      this.ensurePhysiqueTitle();
      this.updatePhysiqueTitleVisibility(false);
      gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
   }
   function InitExtensions()
   {
      _global.gfxExtensions = true;
      Stage.scaleMode = "showAll";
      this.racePanel.Lock("L");
      this.raceDescription.Lock("L");
      this.modeSelect.Lock("TR");
      this.bottomBar.playerInfo.Lock("R");
      this.bottomBar.positionBackground();
      this.vertexEditor.InitExtensions();
      this.modeSelect.InitExtensions();
      this.cameraEditor.InitExtensions();
      this.presetEditor.InitExtensions();
      this._panelX = this.racePanel._x;
      this.colorField._x = this.racePanel._x + this.racePanel._width / 2;
      this.makeupPanel._x = this.racePanel._x + this.racePanel._width / 2;
   }
   function SetPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      this._bPS3Switch = a_bPS3Switch;
      this.itemList.setPlatform(a_platform,a_bPS3Switch);
      this.textEntry.setPlatform(a_platform,a_bPS3Switch);
      this.bottomBar.setPlatform(a_platform,a_bPS3Switch);
      this.colorField.setPlatform(a_platform,a_bPS3Switch);
      this.makeupPanel.setPlatform(a_platform,a_bPS3Switch);
      this.vertexEditor.setPlatform(a_platform,a_bPS3Switch);
      this.cameraEditor.setPlatform(a_platform,a_bPS3Switch);
      this.presetEditor.setPlatform(a_platform,a_bPS3Switch);
      this._acceptControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Ready Weapon",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      this._zoomControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sprint",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      this._activateControl = skyui.defines.Input.Activate;
      if(this._platform == 0)
      {
         this._lightControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sneak",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._searchControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Jump",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._textureControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Wait",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._lightControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Wait",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._textureControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Jump",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._searchControl = null;
      }
      this.textEntry.TextInputInstance.maxChars = 26;
      this.textEntry.SetupButtons();
      this.colorField.SetupButtons();
      this.makeupPanel.SetupButtons();
      var _loc4_ = Stage.visibleRect.x + Stage.safeRect.x;
      var _loc5_ = Stage.visibleRect.x + Stage.visibleRect.width - Stage.safeRect.x;
      this.bottomBar.positionElements(_loc4_,_loc5_);
      this.updateBottomBar();
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      if(this.colorField.enabled)
      {
         return this.colorField.handleInput(details,pathToFocus);
      }
      if(this.textEntry.enabled)
      {
         return this.textEntry.handleInput(details,pathToFocus);
      }
      if(this.makeupPanel.enabled)
      {
         return this.makeupPanel.handleInput(details,pathToFocus);
      }
      if(this.vertexEditor.enabled)
      {
         if(this.vertexEditor.handleInput(details,pathToFocus))
         {
            return true;
         }
         return this.modeSelect.handleInput(details,pathToFocus);
      }
      if(this.cameraEditor.enabled)
      {
         if(this.cameraEditor.handleInput(details,pathToFocus))
         {
            return true;
         }
         return this.modeSelect.handleInput(details,pathToFocus);
      }
      if(this.presetEditor.enabled)
      {
         if(this.presetEditor.handleInput(details,pathToFocus))
         {
            return true;
         }
         return this.modeSelect.handleInput(details,pathToFocus);
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(this.IsBoundKeyPressed(details,this._searchControl,this._platform) && this._platform == 0)
         {
            this.onSearchClicked();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._zoomControl,this._platform) && !this.bTextEntryMode)
         {
            this.onZoomClicked();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._lightControl,this._platform) && !this.bTextEntryMode)
         {
            this.onLightClicked();
            return true;
         }
      }
      if(this.itemList.handleInput(details,pathToFocus))
      {
         return true;
      }
      if(this.categoryList.handleInput(details,pathToFocus))
      {
         return true;
      }
      if(this.modeSelect.handleInput(details,pathToFocus))
      {
         return true;
      }
      return false;
   }
   function ShowTextEntry(abShowTextEntry)
   {
      this.itemList.invalidateSelection();
      this.textEntry._visible = this.textEntry.enabled = abShowTextEntry;
      if(abShowTextEntry)
      {
         if(this.colorField._visible)
         {
            this.ShowColorField(false);
         }
         else if(this.makeupPanel._visible)
         {
            this.ShowMakeupPanel(false);
         }
         this.modeSelect.setMode(0);
         this.vertexEditor.unloadAssets();
         DialogTweenManager.close();
         this.textEntry.updateButtons(true);
         gfx.managers.FocusHandler.instance.setFocus(this.textEntry,0);
      }
      else
      {
         gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
      }
      this.ShowRacePanel(!abShowTextEntry);
      this.ShowBottomBar(!abShowTextEntry);
      this.ShowModeSwitcher(!abShowTextEntry);
      this.syncPhysiqueOverlayState();
      if(abShowTextEntry)
      {
         this.stopPhysiqueWeightSync();
      }
      else
      {
         this.startPhysiqueWeightSync();
      }
   }
   function HideTextEntryWithoutRestoringPanels()
   {
      this.itemList.invalidateSelection();
      this.textEntry.TextInputInstance.focused = false;
      this.textEntry._visible = this.textEntry.enabled = false;
      gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
   }
   function ShowTextEntryField()
   {
      if(this.textEntry.enabled)
      {
         this.textEntry.TextInputInstance.text = this.bottomBar.playerInfo.PlayerName.text;
         this.textEntry.TextInputInstance.focused = true;
         gfx.io.GameDelegate.call("SetAllowTextInput",[]);
         return undefined;
      }
      gfx.io.GameDelegate.call("ShowVirtualKeyboard",[]);
      gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
   }
   function ShowColorField(bShowField, initParams)
   {
      this.colorField._visible = this.colorField.enabled = bShowField;
      this.colorField.ResetSlider();
      if(bShowField)
      {
         this.colorField.initParams = initParams;
         this.colorField.updateButtons(true);
         gfx.managers.FocusHandler.instance.setFocus(this.colorField.colorSelector,0);
      }
      else
      {
         gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
      }
      this.ShowRacePanel(!bShowField);
      this.ShowBottomBar(!bShowField);
      this.ShowModeSwitcher(!bShowField);
      this.syncPhysiqueOverlayState();
      if(bShowField)
      {
         this.stopPhysiqueWeightSync();
      }
      else
      {
         this.startPhysiqueWeightSync();
      }
   }
   function ShowRaceDescription(bShowDescription)
   {
      var _loc3_ = this.raceDescription._visible != bShowDescription;
      this.raceDescription._visible = this.raceDescription.enabled = bShowDescription;
      if(bShowDescription)
      {
         this.itemList.listHeight = this.ITEMLIST_HEIGHT_SHARED;
      }
      else
      {
         this.itemList.listHeight = this.ITEMLIST_HEIGHT_FULL;
      }
      if(_loc3_)
      {
         this.itemList.requestInvalidate();
      }
   }
   function ShowRaceBonuses(a_race, bShowBonuses)
   {
      this.bonusPanel._visible = this.bonusPanel.enabled = bShowBonuses && _global.skse;
      var _loc5_;
      var _loc6_;
      var _loc7_;
      if(bShowBonuses)
      {
         this.bonusList.entryList.splice(0,this.bonusList.entryList.length);
         _loc5_ = 0;
         while(_loc5_ < a_race.skillBonuses.length)
         {
            _loc6_ = a_race.skillBonuses[_loc5_].skill;
            _loc7_ = a_race.skillBonuses[_loc5_].bonus;
            if(!(_loc6_ == RaceMenuDefines.ACTORVALUE_NONE || _loc7_ == 0))
            {
               this.bonusList.entryList.push({text:this.GetActorValueText(_loc6_),value:_loc7_});
            }
            _loc5_ += 1;
         }
         if(this.bonusList.entryList.length == 0 || a_race == undefined)
         {
            this.bonusPanel._visible = this.bonusPanel.enabled = false;
         }
         this.bonusList.requestInvalidate();
      }
      if(this.bonusPanel._visible)
      {
         this.raceDescription.textField._width = this.DESCRIPTION_WIDTH_SHARED;
      }
      else
      {
         this.raceDescription.textField._width = this.DESCRIPTION_WIDTH_FULL;
      }
   }
   function ShowMakeupPanel(bShowPanel, initParams)
   {
      this.makeupPanel._visible = this.makeupPanel.enabled = bShowPanel;
      if(bShowPanel)
      {
         this.makeupPanel.initParams = initParams;
         gfx.managers.FocusHandler.instance.setFocus(this.makeupPanel.makeupList,0);
      }
      else
      {
         gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
      }
      this.ShowRacePanel(!bShowPanel);
      this.ShowBottomBar(!bShowPanel);
      this.ShowModeSwitcher(!bShowPanel);
      this.syncPhysiqueOverlayState();
      if(bShowPanel)
      {
         this.stopPhysiqueWeightSync();
      }
      else
      {
         this.startPhysiqueWeightSync();
      }
   }
   function ShowRacePanel(bShowPanel)
   {
      this.updatePhysiqueTitleVisibility(bShowPanel);
      if(bShowPanel)
      {
         this.categoryList.disableSelection = this.categoryList.disableInput = false;
         this.itemList.disableSelection = this.itemList.disableInput = false;
         this.searchWidget.isDisabled = false;
         com.greensock.TweenLite.to(this.racePanel,0.5,{autoAlpha:100,_x:this._panelX,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
         com.greensock.TweenLite.to(this.raceDescription,0.5,{_alpha:100,_x:this._panelX,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         this.categoryList.disableSelection = this.categoryList.disableInput = true;
         this.itemList.disableSelection = this.itemList.disableInput = true;
         this.searchWidget.isDisabled = true;
         com.greensock.TweenLite.to(this.racePanel,0.5,{autoAlpha:0,_x:this.ITEMLIST_HIDDEN_X,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
         com.greensock.TweenLite.to(this.raceDescription,0.5,{_alpha:0,_x:this.ITEMLIST_HIDDEN_X,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowBottomBar(bShowBottomBar)
   {
      if(bShowBottomBar)
      {
         com.greensock.TweenLite.to(this.bottomBar,0.5,{autoAlpha:100,_y:this.BOTTOMBAR_SHOWN_Y,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.bottomBar,0.5,{autoAlpha:0,_y:this.BOTTOMBAR_HIDDEN_Y,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowModeSwitcher(bShowSwitcher)
   {
      if(bShowSwitcher)
      {
         com.greensock.TweenLite.to(this.modeSelect,0.5,{autoAlpha:100,_y:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.modeSelect,0.5,{autoAlpha:0,_y:-80,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function onNameChange(event)
   {
      if(event.nameChanged == true)
      {
         this.HideTextEntryWithoutRestoringPanels();
         if(_global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory)
         {
            _global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory(0,"");
         }
         this.ShowRacePanel(false);
         this.ShowBottomBar(false);
         this.ShowModeSwitcher(false);
         gfx.io.GameDelegate.call("ChangeName",[this.textEntry.TextInputInstance.text]);
      }
      else
      {
         this.ShowTextEntry(false);
         gfx.io.GameDelegate.call("ChangeName",[]);
      }
      gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeCloseSD"]);
   }
   function HideLoadingIcon()
   {
      this.loadingIcon._visible = false;
      if(this.bRaceChanging)
      {
         skse.SendModEvent(_global.eventPrefix + "RaceChanged");
         this.bRaceChanging = false;
      }
   }
   function SetNameText(astrPlayerName)
   {
      this.bottomBar.playerInfo.PlayerName.SetText(astrPlayerName);
   }
   function SetRaceText(astrPlayerRace)
   {
      this.bottomBar.playerInfo.PlayerRace.SetText(astrPlayerRace);
   }
   function SetCategoriesList()
   {
      this.categoryList.entryList.splice(1,this.categoryList.entryList.length - 1);
      var _loc4_ = RaceMenuDefines.CATEGORY_PRIORITY_START + RaceMenuDefines.CATEGORY_PRIORITY_STEP;
      var _loc5_ = 1;
      var _loc6_ = 0;
      var _loc7_;
      while(_loc6_ < arguments.length)
      {
         _loc7_ = {type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:arguments[_loc6_ + RaceMenuDefines.CAT_TEXT].toUpperCase(),flag:arguments[_loc6_ + RaceMenuDefines.CAT_FLAG],priority:_loc4_,enabled:true};
         _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
         if(this.bLimitedMenu && _loc7_.flag & RaceMenuDefines.CATEGORY_RACE)
         {
            _loc7_.filterFlag = 0;
         }
         this.categoryList.entryList.push(_loc7_);
         _loc5_ += 1;
         _loc6_ += RaceMenuDefines.CAT_STRIDE;
      }
      this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$COLORS",flag:RaceMenuDefines.CATEGORY_COLOR,priority:_loc4_,enabled:true});
      _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
      this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$MAKEUP",flag:RaceMenuDefines.CATEGORY_WARPAINT,priority:_loc4_,enabled:true});
      _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
      var _loc8_;
      var _loc9_;
      var _loc10_;
      var _loc11_;
      if(_global.skse.plugins.NiOverride.bEnableOverlays)
      {
         _loc8_ = _global.skse.plugins.NiOverride.body;
         _loc9_ = _global.skse.plugins.NiOverride.hand;
         _loc10_ = _global.skse.plugins.NiOverride.feet;
         _loc11_ = _global.skse.plugins.NiOverride.face;
         if(_loc8_.iNumOverlays + _loc8_.iSpellOverlays > 0)
         {
            this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$BODY PAINT",flag:RaceMenuDefines.CATEGORY_BODYPAINT,priority:_loc4_,enabled:true});
         }
         _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
         if(_loc9_.iNumOverlays + _loc9_.iSpellOverlays > 0)
         {
            this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$HAND PAINT",flag:RaceMenuDefines.CATEGORY_HANDPAINT,priority:_loc4_,enabled:true});
         }
         _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
         if(_loc10_.iNumOverlays + _loc10_.iSpellOverlays > 0)
         {
            this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$FOOT PAINT",flag:RaceMenuDefines.CATEGORY_FEETPAINT,priority:_loc4_,enabled:true});
         }
         _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
         if(_loc11_.iNumOverlays + _loc11_.iSpellOverlays > 0)
         {
            this.categoryList.entryList.push({type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:"$FACE PAINT",flag:RaceMenuDefines.CATEGORY_FACEPAINT,priority:_loc4_,enabled:true});
         }
         _loc4_ += RaceMenuDefines.CATEGORY_PRIORITY_STEP;
      }
      skse.SendModEvent(_global.eventPrefix + "CategoriesInitialized");
      this.categoryList.requestInvalidate();
   }
   function SetRaceList()
   {
      var _loc3_;
      if(this.itemList.entryList.length > 0)
      {
         _loc3_ = this.itemList.entryList.length;
         while(_loc3_ >= 0)
         {
            if(this.itemList.entryList[_loc3_].type == RaceMenuDefines.ENTRY_TYPE_RACE)
            {
               this.itemList.entryList.splice(_loc3_,1);
            }
            _loc3_ -= 1;
         }
      }
      var _loc4_ = 0;
      var _loc5_ = 0;
      var _loc6_;
      while(_loc5_ < arguments.length)
      {
         _loc6_ = {type:RaceMenuDefines.ENTRY_TYPE_RACE,text:arguments[_loc5_ + RaceMenuDefines.RACE_NAME],filterFlag:RaceMenuDefines.CATEGORY_RACE,raceDescription:(arguments[_loc5_ + RaceMenuDefines.RACE_DESCRIPTION].length > 0 ? arguments[_loc5_ + RaceMenuDefines.RACE_DESCRIPTION] : "No race description for " + arguments[_loc5_ + RaceMenuDefines.RACE_NAME]),equipState:arguments[_loc5_ + RaceMenuDefines.RACE_EQUIPSTATE],raceID:_loc4_,enabled:true};
         _loc6_.isColorEnabled = function()
         {
            return false;
         };
         _loc6_.hasColor = function()
         {
            return false;
         };
         _loc6_.hasGlow = function()
         {
            return false;
         };
         if(_loc6_.equipState > 0)
         {
            this.itemList.listState.activeEntry = _loc6_;
            this.SetRaceText(_loc6_.text);
         }
         this.itemList.entryList.push(_loc6_);
         _loc5_ += RaceMenuDefines.RACE_STRIDE;
         _loc4_ += 1;
      }
      this.itemList.requestInvalidate();
   }
   function SetSliders()
   {
      // The old GFx list can coexist briefly with the new native sex/race.
      // Never drive its Weight row while that list is being replaced.
      this.suspendPhysiqueWeightSyncForRebuild();
      var _loc4_;
      if(this.itemList.entryList.length > 0)
      {
         _loc4_ = this.itemList.entryList.length;
         while(_loc4_ >= 0)
         {
            if(this.itemList.entryList[_loc4_].type == RaceMenuDefines.ENTRY_TYPE_SLIDER)
            {
               this.itemList.entryList.splice(_loc4_,1);
            }
            _loc4_ -= 1;
         }
      }
      this.customSliders.splice(0,this.customSliders.length);
      var _loc5_ = 0;
      var _loc6_ = 0;
      var _loc7_;
      while(_loc6_ < arguments.length)
      {
         _loc7_ = {type:RaceMenuDefines.ENTRY_TYPE_SLIDER,text:arguments[_loc6_ + RaceMenuDefines.SLIDER_NAME],filterFlag:arguments[_loc6_ + RaceMenuDefines.SLIDER_FILTERFLAG],callbackName:arguments[_loc6_ + RaceMenuDefines.SLIDER_CALLBACKNAME],sliderMin:arguments[_loc6_ + RaceMenuDefines.SLIDER_MIN],sliderMax:arguments[_loc6_ + RaceMenuDefines.SLIDER_MAX],sliderID:arguments[_loc6_ + RaceMenuDefines.SLIDER_ID],position:arguments[_loc6_ + RaceMenuDefines.SLIDER_POSITION],interval:arguments[_loc6_ + RaceMenuDefines.SLIDER_INTERVAL],enabled:true};
         if(_loc7_.callbackName == "ChangeTintingMask" || _loc7_.callbackName == "ChangeMaskColor" || _loc7_.callbackName == "ChangeHairColorPreset")
         {
            _loc7_.filterFlag += RaceMenuDefines.CATEGORY_COLOR;
            _loc7_.tintType = RaceMenuDefines.TINT_MAP[_loc5_];
            _loc7_.isColorEnabled = function()
            {
               return _global.skse && (_global.tintCount < _global.maxTints || this.fillColor >>> 24 != 0 || this.tintType == RaceMenuDefines.TINT_TYPE_HAIR) || !_global.skse;
            };
            _loc7_.hasColor = function()
            {
               return true;
            };
            _loc7_.hasGlow = function()
            {
               return false;
            };
            _loc5_ += 1;
         }
         else
         {
            _loc7_.hasColor = function()
            {
               return false;
            };
            _loc7_.hasGlow = function()
            {
               return false;
            };
         }
         if(_loc7_.callbackName == "ChangeWeight")
         {
            _loc7_.interval = 0.01;
         }
         if(_global.skse.plugins.CharGen.GetSliderData)
         {
            if(_loc7_.sliderID != undefined && _loc7_.position != undefined)
            {
               _loc7_.extraData = _global.skse.plugins.CharGen.GetSliderData(_loc7_.sliderID,_loc7_.position);
               switch(_loc7_.extraData.type)
               {
                  case 5:
                     _loc7_.tintType = RaceMenuDefines.TINT_TYPE_HAIR;
                     break;
                  case 4:
                  case 9:
                     _loc7_.tintType = _loc7_.extraData.index;
               }
            }
            else
            {
               _loc7_.extraData = null;
            }
            _loc7_.internalCallback = function()
            {
               if(this.sliderID != undefined && this.position != undefined)
               {
                  this.entryObject.extraData = _global.skse.plugins.CharGen.GetSliderData(this.sliderID,this.position);
               }
               else
               {
                  this.entryObject.extraData = null;
               }
               _root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.updateItemDescriptor();
            };
         }
         _loc7_.GetTextureList = function(raceMenu)
         {
            return null;
         };
         if(_loc7_.callbackName == "ChangeWeight")
         {
            _loc7_.internalCallback = function()
            {
               if(this.sliderID != undefined && this.position != undefined && _global.skse.plugins.CharGen.GetSliderData)
               {
                  this.entryObject.extraData = _global.skse.plugins.CharGen.GetSliderData(this.sliderID,this.position);
               }
               else
               {
                  this.entryObject.extraData = null;
               }
               _root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.updateItemDescriptor();
               if(_global.skse.plugins.ModernPhysiqueSystem.SetWeightSync)
               {
                  _global.skse.plugins.ModernPhysiqueSystem.SetWeightSync(this.entryObject.position);
               }
            };
         }
         else if(_loc7_.callbackName == "ChangeSex" || _loc7_.callbackName == "ChangeRace")
         {
            _loc7_.internalCallback = function()
            {
               if(this.sliderID != undefined && this.position != undefined && _global.skse.plugins.CharGen.GetSliderData)
               {
                  this.entryObject.extraData = _global.skse.plugins.CharGen.GetSliderData(this.sliderID,this.position);
               }
               else
               {
                  this.entryObject.extraData = null;
               }
               var _loc2_ = _root.RaceSexMenuBaseInstance.RaceSexPanelsInstance;
               _loc2_.updateItemDescriptor();
               _loc2_.suspendPhysiqueWeightSyncForRebuild();
            };
         }
         this.itemList.entryList.push(_loc7_);
         _loc6_ += RaceMenuDefines.SLIDER_STRIDE;
      }
      if(!this._sliderInitializeInterval)
      {
         this._sliderInitializeInterval = setInterval(this,"InitializeSliders",500);
      }
   }
   function InitializeSliders()
   {
      if(!this.bMenuInitialized)
      {
         skse.SendModEvent(_global.eventPrefix + "Initialized");
         this.bMenuInitialized = true;
      }
      else
      {
         skse.SendModEvent(_global.eventPrefix + "Reinitialized");
      }
      this.itemList.requestInvalidate();
      this.schedulePhysiqueWeightSyncAfterRebuild();
      clearInterval(this._sliderInitializeInterval);
      delete this._sliderInitializeInterval;
   }
   function SetMakeup(a_types, a_colors, a_textures, a_glows, a_tintType, a_categoryFilter, a_makeupType, a_entryType)
   {
      var _loc10_;
      if(this.itemList.entryList.length > 0)
      {
         _loc10_ = this.itemList.entryList.length;
         while(_loc10_ >= 0)
         {
            if(this.itemList.entryList[_loc10_].type == a_entryType)
            {
               this.itemList.entryList.splice(_loc10_,1);
            }
            _loc10_ -= 1;
         }
      }
      var _loc11_ = 0;
      var _loc12_ = 0;
      var _loc13_;
      var _loc14_;
      var _loc15_;
      var _loc16_;
      var _loc17_;
      var _loc18_;
      var _loc19_;
      var _loc20_;
      var _loc21_;
      while(_loc12_ < a_types.length)
      {
         _loc13_ = a_types[_loc12_];
         _loc14_ = a_textures[_loc12_];
         _loc15_ = _loc11_;
         _loc16_ = a_colors[_loc12_];
         if(_loc13_ == a_tintType)
         {
            _loc11_ += 1;
            _loc17_ = 4278190080;
            if(a_glows)
            {
               _loc17_ = a_glows[_loc12_];
            }
            _loc14_ = _loc14_.split("|")[0];
            _loc18_ = -1;
            _loc10_ = _loc14_.length - 1;
            while(_loc10_ > 0)
            {
               if(_loc14_.charAt(_loc10_) == "\\" || _loc14_.charAt(_loc10_) == "/")
               {
                  _loc18_ = _loc10_;
                  break;
               }
               _loc10_ -= 1;
            }
            _loc19_ = _loc14_.indexOf(".dds");
            if(_loc19_ == -1)
            {
               _loc19_ = _loc14_.length;
            }
            _loc20_ = _loc14_.substring(_loc18_ + 1,_loc19_);
            _loc21_ = {type:a_entryType,listType:a_makeupType,text:_loc20_,texture:_loc14_,tintType:_loc13_,tintIndex:_loc15_,fillColor:_loc16_,glowColor:_loc17_,filterFlag:a_categoryFilter,enabled:true};
            if(a_entryType == RaceMenuDefines.ENTRY_TYPE_WARPAINT)
            {
               _loc21_.isColorEnabled = function(tintColors)
               {
                  return _global.skse && (_global.tintCount < _global.maxTints || this.fillColor >>> 24 != 0);
               };
            }
            else
            {
               _loc21_.isColorEnabled = function(tintColors)
               {
                  return true;
               };
            }
            _loc21_.GetTextureList = function(raceMenu)
            {
               return raceMenu.makeupList[this.listType];
            };
            _loc21_.hasColor = function()
            {
               return true;
            };
            _loc21_.hasGlow = function()
            {
               return this.tintType == RaceMenuDefines.TINT_TYPE_BODYPAINT || this.tintType == RaceMenuDefines.TINT_TYPE_HANDPAINT || this.tintType == RaceMenuDefines.TINT_TYPE_FEETPAINT || this.tintType == RaceMenuDefines.TINT_TYPE_FACEPAINT;
            };
            this.itemList.entryList.push(_loc21_);
         }
         else
         {
            _loc11_ = 0;
         }
         _loc12_ += 1;
      }
      this.itemList.requestInvalidate();
   }
   function onBonusFilterChange()
   {
      this.bonusList.requestInvalidate();
   }
   function onFilterChange()
   {
      this.itemList.requestInvalidate();
   }
   function onCategoryFilterChange()
   {
      this.categoryList.requestInvalidate();
   }
   function onSearchInputStart(event)
   {
      this.bTextEntryMode = true;
      this.categoryList.disableSelection = this.categoryList.disableInput = true;
      this.itemList.disableSelection = this.itemList.disableInput = true;
      this._nameFilter.filterText = "";
   }
   function onSearchInputChange(event)
   {
      this._nameFilter.filterText = event.data;
   }
   function onSearchInputEnd(event)
   {
      this.categoryList.disableSelection = this.categoryList.disableInput = false;
      this.itemList.disableSelection = this.itemList.disableInput = false;
      this._nameFilter.filterText = event.data;
      this.bTextEntryMode = false;
   }
   function getWeightSliderEntry()
   {
      var _loc2_ = 0;
      while(_loc2_ < this.itemList.entryList.length)
      {
         if(this.itemList.entryList[_loc2_].callbackName == "ChangeWeight")
         {
            return this.itemList.entryList[_loc2_];
         }
         _loc2_ += 1;
      }
      return null;
   }
   function isPhysiqueWeightSyncContextActive()
   {
      return this.bPhysiqueBodyCategoryActive && this.bPhysiqueSlidersModeActive && !this.textEntry.enabled && !this.colorField.enabled && !this.makeupPanel.enabled;
   }
   function syncWeightSliderFromPhysique()
   {
      if(!this.isPhysiqueWeightSyncContextActive() || this.bPhysiqueWeightSyncRebuilding)
      {
         return undefined;
      }
      if(!_global.skse || !_global.skse.plugins.ModernPhysiqueSystem.GetWeightSync)
      {
         return undefined;
      }
      var _loc3_ = this.getWeightSliderEntry();
      if(!_loc3_)
      {
         return undefined;
      }
      var _loc2_ = Number(_global.skse.plugins.ModernPhysiqueSystem.GetWeightSync());
      if(isNaN(_loc2_))
      {
         return undefined;
      }
      _loc2_ = Math.max(0,Math.min(1,_loc2_));
      if(Math.abs(_loc3_.position - _loc2_) <= 0.003)
      {
         return undefined;
      }
      _loc3_.position = _loc2_;
      this.itemList.requestUpdate();
   }
   function startPhysiqueWeightSync()
   {
      if(!this.isPhysiqueWeightSyncContextActive() || this.bPhysiqueWeightSyncRebuilding)
      {
         return undefined;
      }
      this.stopPhysiqueWeightSync();
      this.cancelPhysiqueWeightSyncResume();
      this.syncWeightSliderFromPhysique();
      this.physiqueWeightSyncInterval = setInterval(this,"syncWeightSliderFromPhysique",50);
   }
   function suspendPhysiqueWeightSyncForRebuild()
   {
      this.bPhysiqueWeightSyncRebuilding = true;
      this.stopPhysiqueWeightSync();
      this.cancelPhysiqueWeightSyncResume();
   }
   function schedulePhysiqueWeightSyncAfterRebuild()
   {
      this.stopPhysiqueWeightSync();
      this.cancelPhysiqueWeightSyncResume();
      // Always hold the gate while RSM_(Re)initialized and list invalidation
      // settle. The callback decides whether Body/Sliders still needs polling.
      this.physiqueWeightSyncResumeInterval = setInterval(this,"resumePhysiqueWeightSyncAfterRebuild",100);
   }
   function resumePhysiqueWeightSyncAfterRebuild()
   {
      this.cancelPhysiqueWeightSyncResume();
      this.bPhysiqueWeightSyncRebuilding = false;
      if(this.isPhysiqueWeightSyncContextActive())
      {
         this.startPhysiqueWeightSync();
      }
   }
   function stopPhysiqueWeightSync()
   {
      if(this.physiqueWeightSyncInterval != null)
      {
         clearInterval(this.physiqueWeightSyncInterval);
         delete this.physiqueWeightSyncInterval;
      }
   }
   function cancelPhysiqueWeightSyncResume()
   {
      if(this.physiqueWeightSyncResumeInterval != null)
      {
         clearInterval(this.physiqueWeightSyncResumeInterval);
         delete this.physiqueWeightSyncResumeInterval;
      }
   }
   function syncPhysiqueOverlayState()
   {
      if(!_global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory)
      {
         return undefined;
      }
      if(this.isPhysiqueWeightSyncContextActive() && this.categoryList.selectedEntry != undefined)
      {
         _global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory(this.categoryList.selectedEntry.flag,this.categoryList.selectedEntry.textFilter);
      }
      else
      {
         _global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory(0,"");
      }
   }
   function getPhysiqueTitleAnchorClip()
   {
      var _loc2_ = this.getWeightSliderEntry();
      if(_loc2_ && _loc2_.clipIndex != undefined)
      {
         return this.itemList.getClipByIndex(_loc2_.clipIndex);
      }
      return null;
   }
   function refreshPhysiqueTitleRow()
   {
      if(this.physiqueTitle == null)
      {
         return undefined;
      }
      var _loc3_ = this.getPhysiqueTitleAnchorClip();
      var _loc2_ = {type:RaceMenuDefines.ENTRY_TYPE_RACE,text:"Physique:",enabled:true,align:"left"};
      _loc2_.isColorEnabled = function()
      {
         return false;
      };
      _loc2_.hasColor = function()
      {
         return false;
      };
      _loc2_.hasGlow = function()
      {
         return false;
      };
      this.physiqueTitle.setEntry(_loc2_,{list:{selectedEntry:null},activeEntry:null,focusEntry:null});
      if(_loc3_)
      {
         this.physiqueTitle._x = _loc3_._x;
         this.physiqueTitle._y = _loc3_._y + this.itemList.entryHeight;
      }
      else
      {
         this.physiqueTitle._x = this.itemList.background._x + this.itemList.leftBorder;
         this.physiqueTitle._y = this.itemList.background._y + this.itemList.topBorder + this.itemList.entryHeight * 4;
      }
      this.physiqueTitle.itemIndex = undefined;
      this.physiqueTitle.trigger._visible = false;
      this.physiqueTitle.trigger.enabled = false;
      this.physiqueTitle.activeIndicator._visible = false;
      this.physiqueTitle.activeIndicator.enabled = false;
      this.physiqueTitle.selectIndicator._visible = false;
      this.physiqueTitle.focusIndicator._visible = false;
      this.physiqueTitle.enabled = false;
   }
   function ensurePhysiqueTitle()
   {
      if(this.physiqueTitle != null)
      {
         return undefined;
      }
      this.physiqueTitle = SliderListEntry(this.itemList.attachMovie("SliderListEntry","physiqueTitle",this.itemList.getNextHighestDepth()));
      this.physiqueTitle.onSliderLoad = function(event)
      {
         return undefined;
      };
      this.physiqueTitle.textField.textColor = SliderListEntry.defaultTextColor;
      this.physiqueTitle._visible = false;
      this.refreshPhysiqueTitleRow();
   }
   function updatePhysiqueTitleVisibility(bShowPanel)
   {
      this.ensurePhysiqueTitle();
      if(this.bPhysiqueBodyCategoryActive && bShowPanel)
      {
         this.refreshPhysiqueTitleRow();
      }
      this.physiqueTitle._visible = this.bPhysiqueBodyCategoryActive && bShowPanel;
   }
   function onCategoryPress(a_event)
   {
      if(this.categoryList.selectedEntry != undefined)
      {
         this._typeFilter.changeFilterData(this.categoryList.selectedEntry.flag,this.categoryList.selectedEntry.textFilter);
         this.bPhysiqueBodyCategoryActive = this.categoryList.selectedEntry.flag == RaceMenuDefines.CATEGORY_BODY;
         this.updatePhysiqueTitleVisibility(true);
         if(this.bPhysiqueBodyCategoryActive)
         {
            this.startPhysiqueWeightSync();
            if(this.bPlayerZoom)
            {
               this.bPlayerZoom = false;
               gfx.io.GameDelegate.call("ZoomPC",[this.bPlayerZoom]);
               this.updateBottomBar();
            }
         }
         else if(!this.bPlayerZoom)
         {
            this.stopPhysiqueWeightSync();
            this.bPlayerZoom = true;
            gfx.io.GameDelegate.call("ZoomPC",[this.bPlayerZoom]);
            this.updateBottomBar();
         }
         else
         {
            this.stopPhysiqueWeightSync();
         }
         this.syncPhysiqueOverlayState();
      }
   }
   function onCategoryChange(a_event)
   {
      this.itemList.listState.focusEntry = null;
      this.itemList.selectedIndex = -1;
      gfx.io.GameDelegate.call("PlaySound",["UIMenuPrevNext"]);
   }
   function onChangeTexture(event)
   {
      var _loc3_;
      if(event.texture != undefined)
      {
         _loc3_ = this.itemList.listState.selectedEntry;
         if(this.isMakeup(_loc3_.tintType))
         {
            if(event.apply)
            {
               _loc3_.text = event.displayText;
               _loc3_.texture = event.texture;
            }
            this.requestUpdate({type:"makeupTexture",tintType:_loc3_.tintType,tintIndex:_loc3_.tintIndex,replacementTexture:event.texture});
         }
         if(event.apply)
         {
            this.itemList.requestUpdate();
         }
      }
      if(event.apply)
      {
         this.ShowMakeupPanel(false);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeCloseSD"]);
      }
   }
   function onChangeMode(event)
   {
      this.bPhysiqueSlidersModeActive = event.index == 0;
      if(this.bPhysiqueSlidersModeActive)
      {
         this.startPhysiqueWeightSync();
      }
      else
      {
         this.stopPhysiqueWeightSync();
      }
      switch(event.index)
      {
         case 0:
            this.ShowRacePanel(true);
            this.ShowBottomBar(true);
            this.vertexEditor.ShowAll(false,false,true);
            this.cameraEditor.ShowAll(false);
            this.presetEditor.ShowAll(false);
            this.syncPhysiqueOverlayState();
            return undefined;
         case 1:
            this.ShowRacePanel(false);
            this.ShowBottomBar(false);
            this.vertexEditor.ShowAll(false,false,false);
            this.cameraEditor.ShowAll(false);
            this.presetEditor.ShowAll(true);
            this.syncPhysiqueOverlayState();
            return undefined;
         case 2:
            this.ShowRacePanel(false);
            this.ShowBottomBar(false);
            this.cameraEditor.ShowAll(true);
            this.vertexEditor.ShowAll(false,false,false);
            this.presetEditor.ShowAll(false);
            this.syncPhysiqueOverlayState();
            return undefined;
         case 3:
            this.ShowRacePanel(true);
            this.ShowRacePanel(false);
            this.ShowBottomBar(false);
            this.vertexEditor.ShowAll(true,true,false);
            this.cameraEditor.ShowAll(false);
            this.presetEditor.ShowAll(false);
            this.syncPhysiqueOverlayState();
      }
      return undefined;
   }
   function onTabRollOver(event)
   {
      switch(event.index)
      {
         case 0:
            if(this.vertexEditor.hasAssets())
            {
               this.setStatusText("$Sculpt history will be lost",3);
               return undefined;
            }
            return undefined;
            break;
         case 1:
            if(this.vertexEditor.hasAssets())
            {
               this.setStatusText("$Loading a preset will erase sculpt history",3);
               return undefined;
            }
            return undefined;
            break;
         case 2:
         case 3:
            if(this.vertexEditor.hasAssets())
            {
               this.setStatusText("__CLEAR__",3);
            }
      }
      return undefined;
   }
   function onImportedFile(event)
   {
      if(event.success)
      {
         this.setStatusText("$Imported data from {" + event.name + "}",3);
      }
      else
      {
         this.setStatusText("$Failed to import data from {" + event.name + "}",3);
      }
   }
   function onLoadPreset(event)
   {
      if(event.success)
      {
         this.vertexEditor.unloadAssets();
         skse.SendModEvent(_global.eventPrefix + "RequestTintSave");
         this.requestSliderUpdate(event);
      }
      else
      {
         this.setStatusText("$Failed to load preset from {" + event.name + "}",3);
      }
   }
   function onSavePreset(event)
   {
      if(event.success)
      {
         this.setStatusText("$Saved preset to {" + event.name + "}",3);
      }
      else
      {
         this.setStatusText("$Failed to save preset to {" + event.name + "}",3);
      }
   }
   function onChangeColor(event)
   {
      var _loc4_;
      if(event.color != undefined)
      {
         _loc4_ = event.entry;
         switch(event.mode)
         {
            case "tint":
               if(_loc4_.filterFlag & RaceMenuDefines.CATEGORY_COLOR)
               {
                  if(event.apply)
                  {
                     if(_loc4_.tintType != RaceMenuDefines.TINT_TYPE_HAIR && _loc4_.fillColor >>> 24 == 0 && event.color >>> 24 != 0)
                     {
                        _global.tintCount += 1;
                        this.UpdateTintCount();
                     }
                     else if(_loc4_.tintType != RaceMenuDefines.TINT_TYPE_HAIR && _loc4_.fillColor >>> 24 != 0 && event.color >>> 24 == 0)
                     {
                        _global.tintCount -= 1;
                        this.UpdateTintCount();
                     }
                     _loc4_.fillColor = event.color;
                  }
                  this.requestUpdate({type:"sliderColor",slider:_loc4_,argbColor:event.color});
                  break;
               }
               if(this.isMakeup(_loc4_.tintType))
               {
                  if(event.apply)
                  {
                     if(_loc4_.tintType != RaceMenuDefines.TINT_TYPE_HAIR && _loc4_.fillColor >>> 24 == 0 && event.color >>> 24 != 0)
                     {
                        _global.tintCount += 1;
                        this.UpdateTintCount();
                     }
                     else if(_loc4_.tintType != RaceMenuDefines.TINT_TYPE_HAIR && _loc4_.fillColor >>> 24 != 0 && event.color >>> 24 == 0)
                     {
                        _global.tintCount -= 1;
                        this.UpdateTintCount();
                     }
                     _loc4_.fillColor = event.color;
                  }
                  this.requestUpdate({type:"makeupColor",tintType:_loc4_.tintType,tintIndex:_loc4_.tintIndex,argbColor:event.color});
               }
               break;
            case "glow":
               if(event.apply)
               {
                  _loc4_.glowColor = event.color;
               }
               this.requestUpdate({type:"glowColor",tintType:_loc4_.tintType,tintIndex:_loc4_.tintIndex,argbColor:event.color});
         }
         if(event.apply)
         {
            this.itemList.requestUpdate();
         }
      }
      if(event.closeField)
      {
         this.ShowColorField(false);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeCloseSD"]);
      }
   }
   function isMakeup(tintType)
   {
      return tintType == RaceMenuDefines.TINT_TYPE_WARPAINT || this.isOverlayType(tintType);
   }
   function isOverlayType(tintType)
   {
      return tintType == RaceMenuDefines.TINT_TYPE_BODYPAINT || tintType == RaceMenuDefines.TINT_TYPE_HANDPAINT || tintType == RaceMenuDefines.TINT_TYPE_FEETPAINT || tintType == RaceMenuDefines.TINT_TYPE_FACEPAINT;
   }
   function requestUpdate(pendingData)
   {
      this._pendingData = pendingData;
      if(!this._updateInterval)
      {
         this._updateInterval = setInterval(this,"processDataUpdate",100,pendingData);
      }
   }
   function requestSliderUpdate(pendingData)
   {
      if(!this._reloadInterval)
      {
         this._reloadInterval = setInterval(this,"ReloadSliders",500,pendingData);
      }
   }
   function processDataUpdate()
   {
      switch(this._pendingData.type)
      {
         case "makeupTexture":
            this.SendPlayerTexture(this._pendingData.tintType,this._pendingData.tintIndex,this._pendingData.replacementTexture);
            break;
         case "makeupColor":
            this.SendPlayerTint(this._pendingData.tintType,this._pendingData.tintIndex,this._pendingData.argbColor);
            break;
         case "sliderColor":
            this.SendPlayerTintBySlider(this._pendingData.slider,this._pendingData.argbColor);
            break;
         case "glowColor":
            this.SendPlayerGlow(this._pendingData.tintType,this._pendingData.tintIndex,this._pendingData.argbColor);
      }
      this._pendingData = null;
      clearInterval(this._updateInterval);
      delete this._updateInterval;
   }
   function SendPlayerTintBySlider(slider, argbColor)
   {
      var _loc4_ = slider.tintType;
      var _loc5_ = 0;
      if(argbColor == undefined)
      {
         skyui.util.Debug.log("RSM Warning: Invalid color.");
         return undefined;
      }
      var _loc6_;
      if(_loc4_ == RaceMenuDefines.TINT_TYPE_WARPAINT)
      {
         _loc6_ = 0;
         while(_loc6_ < this.itemList.entryList.length)
         {
            if(this.itemList.entryList[_loc6_].sliderID == RaceMenuDefines.STATIC_SLIDER_WARPAINT)
            {
               _loc5_ = this.itemList.entryList[_loc6_].position;
               break;
            }
            _loc6_ += 1;
         }
      }
      if(_loc4_ == RaceMenuDefines.TINT_TYPE_DIRT)
      {
         _loc6_ = 0;
         while(_loc6_ < this.itemList.entryList.length)
         {
            if(this.itemList.entryList[_loc6_].sliderID == RaceMenuDefines.STATIC_SLIDER_DIRT)
            {
               _loc5_ = this.itemList.entryList[_loc6_].position;
               break;
            }
            _loc6_ += 1;
         }
      }
      this.SendPlayerTint(_loc4_,_loc5_,argbColor);
   }
   function SendPlayerTexture(tintType, tintIndex, replacementTexture)
   {
      if(this.isOverlayType(tintType))
      {
         skse.SendModEvent(_global.eventPrefix + "OverlayTextureChange",replacementTexture,tintType * 1000 + tintIndex);
      }
      else
      {
         skse.SendModEvent(_global.eventPrefix + "TintTextureChange",replacementTexture,tintType * 1000 + tintIndex);
      }
   }
   function SendPlayerTint(tintType, tintIndex, argbColor)
   {
      if(tintType == RaceMenuDefines.TINT_TYPE_HAIR)
      {
         skse.SendModEvent(_global.eventPrefix + "HairColorChange",Number(argbColor).toString(),0);
      }
      else if(this.isOverlayType(tintType))
      {
         skse.SendModEvent(_global.eventPrefix + "OverlayColorChange",Number(argbColor).toString(),tintType * 1000 + tintIndex);
      }
      else if(tintType != -1)
      {
         skse.SendModEvent(_global.eventPrefix + "TintColorChange",Number(argbColor).toString(),tintType * 1000 + tintIndex);
      }
   }
   function SendPlayerGlow(tintType, tintIndex, argbColor)
   {
      skse.SendModEvent(_global.eventPrefix + "OverlayGlowColorChange",Number(argbColor).toString(),tintType * 1000 + tintIndex);
   }
   function onItemPress(event)
   {
      var _loc3_ = this.itemList.entryList[event.index];
      if(_loc3_ != this.itemList.listState.activeEntry && _loc3_.type == RaceMenuDefines.ENTRY_TYPE_RACE)
      {
         this.itemList.listState.activeEntry = _loc3_;
         this.itemList.requestUpdate();
         this.loadingIcon._visible = true;
         this.suspendPhysiqueWeightSyncForRebuild();
         gfx.io.GameDelegate.call("ChangeRace",[_loc3_.raceID,-1]);
         this.bRaceChanging = true;
         this.bPlayerZoom = true;
         this.updateBottomBar();
      }
      else if(_loc3_.isColorEnabled())
      {
         this.colorField.setText(_loc3_.text);
         this.colorField.setColor(_loc3_.fillColor);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
         this.ShowColorField(true,{entry:_loc3_,mode:"tint",bCanSwitchMode:this.isOverlayType(_loc3_.tintType),savedColor:this._savedColor});
      }
   }
   function onItemPressSecondary(event)
   {
      var _loc3_ = this.itemList.entryList[event.index];
      var _loc4_ = _loc3_.GetTextureList(this);
      if(_loc4_)
      {
         this.makeupPanel.setMakeupList(_loc4_);
         this.makeupPanel.updateButtons(true);
         this.makeupPanel.setSelectedEntry(_loc3_.texture);
         this.makeupPanel.setTexture(_loc3_.text,_loc3_.texture);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
         this.ShowMakeupPanel(true,{entry:_loc3_});
      }
   }
   function onItemPressAux(event)
   {
      var _loc3_ = this.itemList.entryList[event.index];
      if(_loc3_.hasGlow() && this.isOverlayType(_loc3_.tintType))
      {
         this.colorField.setText(_loc3_.text);
         this.colorField.setColor(_loc3_.glowColor);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
         this.ShowColorField(true,{entry:_loc3_,mode:"glow",bCanSwitchMode:true,savedColor:this._savedColor});
      }
   }
   function onSwitchColorMode(event)
   {
      switch(event.mode)
      {
         case "tint":
            this.colorField.mode = "glow";
            this.colorField.disableAlpha(false);
            this.colorField.setColor(this.colorField.entry.glowColor);
            return undefined;
         case "glow":
            this.colorField.mode = "tint";
            this.colorField.disableAlpha(false);
            this.colorField.setColor(this.colorField.entry.fillColor);
      }
      return undefined;
   }
   function onSaveColor(event)
   {
      this._savedColor = event.color;
      this.setDisplayText("$Saved Color",this._savedColor);
   }
   function onSelectionChange(event)
   {
      var _loc3_ = this.itemList.entryList[event.index];
      this.itemList.listState.selectedEntry = _loc3_;
      if(_loc3_.filterFlag & RaceMenuDefines.CATEGORY_RACE)
      {
         this.raceDescription.textField.SetText(_loc3_.raceDescription);
         this.ShowRaceDescription(true);
         this.ShowRaceBonuses(this._raceList[_loc3_.raceID],true);
      }
      else
      {
         this.ShowRaceBonuses(null,false);
         this.ShowRaceDescription(false);
         this.raceDescription.textField.SetText("");
      }
      gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
      this.updateItemDescriptor();
      this.updateBottomBar();
   }
   function updateItemDescriptor(a_fadeDelay)
   {
      var _loc4_ = this.itemList.listState.selectedEntry;
      var _loc5_;
      var _loc6_;
      var _loc7_;
      var _loc8_;
      if(_loc4_.extraData && this.itemList.disableInput == false)
      {
         _loc5_ = _loc4_.extraData.partName;
         _loc6_ = _loc4_.extraData.formId;
         if(_loc5_ != undefined && _loc6_ != undefined)
         {
            _loc7_ = _global.skse.plugins.CharGen.GetModName(_loc6_);
            this.itemDescriptor.setText(_loc7_ + " (" + _loc5_ + ")",a_fadeDelay);
            this.itemDescriptor._x = Stage.visibleRect.x + Stage.visibleRect.width - this.itemDescriptor._width;
            this.itemDescriptor.toggle(true);
            this.itemDescriptor.fadeOut();
         }
         else
         {
            this.itemDescriptor.toggle(false);
         }
      }
      else if(_loc4_.filterFlag & RaceMenuDefines.CATEGORY_RACE && this.itemList.disableInput == false)
      {
         _loc8_ = this._raceList[_loc4_.raceID];
         if(_loc8_ != undefined)
         {
            _loc5_ = _loc8_.editorId;
            _loc6_ = _loc8_.formId;
            _loc7_ = _global.skse.plugins.CharGen.GetModName(_loc6_);
            this.itemDescriptor.setText(_loc7_ + " (" + _loc5_ + ")",a_fadeDelay);
            this.itemDescriptor._x = Stage.visibleRect.x + Stage.visibleRect.width - this.itemDescriptor._width;
            this.itemDescriptor.toggle(true);
            this.itemDescriptor.fadeOut();
         }
         else
         {
            this.itemDescriptor.toggle(false);
         }
      }
      else if(this.statusText != "")
      {
         if(this.statusText != "__CLEAR__")
         {
            this.itemDescriptor.setText(this.statusText,a_fadeDelay);
            this.itemDescriptor._x = Stage.visibleRect.x + Stage.visibleRect.width - this.itemDescriptor._width;
            this.itemDescriptor.toggle(true);
            this.itemDescriptor.fadeOut();
         }
         else
         {
            this.statusText = "";
            this.itemDescriptor.toggle(false);
         }
      }
      else
      {
         this.itemDescriptor.toggle(false);
      }
   }
   function updateBottomBar()
   {
      this.navPanel.clearButtons();
      this.navPanel.addButton({text:"$Done",controls:this._acceptControl}).addEventListener("click",this,"onDoneClicked");
      if(this._platform == 0)
      {
         this.navPanel.addButton({text:"$Search",controls:this._searchControl}).addEventListener("click",this,"onSearchClicked");
      }
      this.navPanel.addButton({text:(!this.bPlayerZoom ? "$Zoom In" : "$Zoom Out"),controls:this._zoomControl}).addEventListener("click",this,"onZoomClicked");
      if(_global.skse)
      {
         this.navPanel.addButton({text:(!this.bShowLight ? "$Light On" : "$Light Off"),controls:this._lightControl}).addEventListener("click",this,"onLightClicked");
      }
      var _loc3_ = this.itemList.listState.selectedEntry;
      if(_loc3_ != this.itemList.listState.activeEntry && _loc3_.filterFlag & RaceMenuDefines.CATEGORY_RACE)
      {
         this.navPanel.addButton({text:"$Change Race",controls:this._activateControl}).addEventListener("click",this,"onChangeRaceClicked");
      }
      if(_loc3_.isColorEnabled())
      {
         this.navPanel.addButton({text:"$Choose Color",controls:this._activateControl}).addEventListener("click",this,"onChooseColorClicked");
      }
      if(_loc3_.GetTextureList(this))
      {
         this.navPanel.addButton({text:"$Choose Texture",controls:this._textureControl}).addEventListener("click",this,"onChooseTextureClicked");
      }
      this.navPanel.updateButtons(true);
   }
   function setDisplayText(a_text, a_color)
   {
      this.textDisplay._visible = true;
      this.textDisplay._alpha = 100;
      this.textDisplay.text = skyui.util.Translator.translateNested(a_text);
      if(a_color != undefined)
      {
         this.textDisplay.textColor = a_color;
      }
      else
      {
         this.textDisplay.textColor = 16777215;
      }
      com.greensock.TweenLite.to(this.textDisplay,2.5,{autoAlpha:0,easing:com.greensock.easing.Linear.easeNone});
   }
   function setStatusText(a_text, a_fadeDelay)
   {
      this.statusText = skyui.util.Translator.translateNested(a_text);
      this.updateItemDescriptor(a_fadeDelay);
      this.statusText = "";
   }
   function onDoneClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      if(this._sliderInitializeInterval != null)
      {
         clearInterval(this._sliderInitializeInterval);
         delete this._sliderInitializeInterval;
      }
      this.stopPhysiqueWeightSync();
      this.cancelPhysiqueWeightSyncResume();
      this.bPhysiqueWeightSyncRebuilding = false;
      if(_global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory)
      {
         _global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory(0,"");
      }
      gfx.io.GameDelegate.call("ConfirmDone",[]);
   }
   function onSearchClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      this.searchWidget.startInput();
   }
   function onZoomClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      this.bPlayerZoom = !this.bPlayerZoom;
      gfx.io.GameDelegate.call("ZoomPC",[this.bPlayerZoom]);
      this.updateBottomBar();
   }
   function onLightClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      this.bShowLight = !this.bShowLight;
      skse.SendModEvent(_global.eventPrefix + "ToggleLight","",Number(this.bShowLight));
      this.updateBottomBar();
   }
   function onChangeRaceClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      var _loc2_ = this.itemList.listState.selectedEntry;
      if(_loc2_)
      {
         this.itemList.listState.activeEntry = _loc2_;
         this.itemList.requestUpdate();
         this.loadingIcon._visible = true;
         this.suspendPhysiqueWeightSyncForRebuild();
         gfx.io.GameDelegate.call("ChangeRace",[_loc2_.raceID,-1]);
         this.bRaceChanging = true;
         this.bPlayerZoom = true;
         this.updateBottomBar();
      }
   }
   function onChooseColorClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      var _loc2_ = this.itemList.listState.selectedEntry;
      if(_loc2_.isColorEnabled())
      {
         this.colorField.setText(_loc2_.text);
         this.colorField.setColor(_loc2_.fillColor);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
         this.ShowColorField(true,{entry:_loc2_,mode:"tint",bCanSwitchMode:this.isOverlayType(_loc2_.tintType),savedColor:this._savedColor});
      }
   }
   function onChooseTextureClicked()
   {
      if(this.colorField._visible || this.textEntry._visible || this.makeupPanel._visible)
      {
         return undefined;
      }
      var _loc2_ = this.itemList.listState.selectedEntry;
      var _loc3_ = _loc2_.GetTextureList(this);
      if(_loc3_)
      {
         this.makeupPanel.setMakeupList(_loc3_);
         this.makeupPanel.updateButtons(true);
         this.makeupPanel.setSelectedEntry(_loc2_.texture);
         this.makeupPanel.setTexture(_loc2_.text,_loc2_.texture);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuBladeOpenSD"]);
         this.ShowMakeupPanel(true,{entry:_loc2_});
      }
   }
   function ReloadSliders(event)
   {
      _global.skse.plugins.CharGen.ReloadSliders();
      skse.SendModEvent(_global.eventPrefix + "RequestTintLoad");
      this.SendPlayerTint(RaceMenuDefines.TINT_TYPE_HAIR,0,event.data.hairColor);
      this.setStatusText("$Loaded preset from {" + event.name + "}",3);
      this.loadingIcon._visible = false;
      clearInterval(this._reloadInterval);
      delete this._reloadInterval;
   }
   function GetSliderByType(tintType)
   {
      var _loc3_ = 0;
      while(_loc3_ < this.itemList.entryList.length)
      {
         if(this.itemList.entryList[_loc3_].tintType == tintType)
         {
            return this.itemList.entryList[_loc3_];
         }
         _loc3_ += 1;
      }
      return null;
   }
   function GetActorValueText(a_actorValue)
   {
      var _loc2_ = 0;
      while(_loc2_ < RaceMenuDefines.ACTORVALUE_MAP.length)
      {
         if(RaceMenuDefines.ACTORVALUE_MAP[_loc2_].value == a_actorValue)
         {
            return RaceMenuDefines.ACTORVALUE_MAP[_loc2_].text;
         }
         _loc2_ += 1;
      }
      return "UNKAV " + a_actorValue;
   }
   function SetSliderColors(tintTypes, tintColors)
   {
      var _loc4_ = new Array();
      var _loc5_ = 0;
      var _loc6_;
      var _loc7_;
      var _loc8_;
      while(_loc5_ < tintTypes.length)
      {
         if(tintTypes[_loc5_] != 0 && tintColors[_loc5_] != 0)
         {
            _loc6_ = false;
            _loc7_ = 0;
            while(_loc7_ < _loc4_.length)
            {
               if(_loc4_[_loc7_] == tintTypes[_loc5_])
               {
                  _loc6_ = true;
                  break;
               }
               _loc7_ += 1;
            }
            if(!(_loc6_ && tintColors[_loc5_] <= 16777215))
            {
               _loc8_ = this.GetSliderByType(tintTypes[_loc5_]);
               if(_loc8_)
               {
                  _loc8_.fillColor = tintColors[_loc5_];
                  if(!_loc6_)
                  {
                     _loc4_.push(tintTypes[_loc5_]);
                  }
               }
            }
         }
         _loc5_ += 1;
      }
   }
   function UpdateTintCount()
   {
      this.racePanel.tintCount.text = "(" + _global.tintCount + "/" + _global.maxTints + ")";
   }
   function AddMakeup(a_type, a_array, a_name, a_texture)
   {
      var _loc5_ = {type:a_type,text:a_name,texture:a_texture,filterFlag:1,enabled:true};
      var _loc6_ = -1;
      var _loc7_ = a_texture.length - 1;
      while(_loc7_ > 0)
      {
         if(a_texture.charAt(_loc7_) == "\\" || a_texture.charAt(_loc7_) == "/")
         {
            _loc6_ = _loc7_;
            break;
         }
         _loc7_ -= 1;
      }
      var _loc8_ = a_texture.lastIndexOf(".dds");
      if(_loc8_ == -1)
      {
         _loc8_ = a_texture.length;
      }
      var _loc9_ = a_texture.substring(_loc6_ + 1,_loc8_);
      _loc5_.displayText = _loc9_;
      a_array.push(_loc5_);
   }
}
