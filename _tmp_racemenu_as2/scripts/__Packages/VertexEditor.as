class VertexEditor extends MovieClip
{
   var _acceptControl;
   var _bPS3Switch;
   var _clearSculptControl;
   var _downControl;
   var _exportHeadControl;
   var _importData;
   var _importHeadControl;
   var _importName;
   var _importPath;
   var _leftControl;
   var _lrControl;
   var _modifierControl;
   var _platform;
   var _rightControl;
   var _udControl;
   var _upControl;
   var bottomBar;
   var brushWindow;
   var dispatchEvent;
   var historyWindow;
   var meshWindow;
   var navPanel;
   var tempText;
   var wireframeDisplay;
   var BOTTOMBAR_SHOWN_Y = 645;
   var BOTTOMBAR_HIDDEN_Y = 745;
   var _secondary = false;
   function VertexEditor()
   {
      super();
      gfx.events.EventDispatcher.initialize(this);
      this.navPanel = this.bottomBar.buttonPanel;
      this.wireframeDisplay._visible = this.wireframeDisplay.enabled = false;
      this.wireframeDisplay._alpha = 0;
      this.meshWindow._visible = this.meshWindow.enabled = false;
      this.meshWindow._alpha = 0;
      this.historyWindow._visible = this.historyWindow.enabled = false;
      this.historyWindow._alpha = 0;
      this.brushWindow._visible = this.brushWindow.enabled = false;
      this.brushWindow._alpha = 0;
      this.tempText._visible = this.tempText.enabled = false;
      this.tempText._alpha = 0;
      this.bottomBar._y = this.BOTTOMBAR_HIDDEN_Y;
   }
   function onLoad()
   {
      super.onLoad();
      this.wireframeDisplay.addEventListener("beginPainting",this,"onBeginActivity");
      this.wireframeDisplay.addEventListener("endPainting",this,"onEndActivity");
      this.wireframeDisplay.addEventListener("beginRotating",this,"onBeginActivity");
      this.wireframeDisplay.addEventListener("endRotating",this,"onEndActivity");
      this.wireframeDisplay.addEventListener("beginPanning",this,"onBeginActivity");
      this.wireframeDisplay.addEventListener("endPanning",this,"onEndActivity");
      this.brushWindow.addEventListener("changeBrush",this,"onChangeBrush");
      this.bottomBar.playerInfo.RaceLabel.enabled = this.bottomBar.playerInfo.RaceLabel._visible = false;
      this.bottomBar.playerInfo.PlayerRace.enabled = this.bottomBar.playerInfo.PlayerRace._visible = false;
      this.bottomBar.playerInfo.NameLabel.text = "$Brush";
      var _loc3_ = this.bottomBar.attachMovie("ButtonPanel","staticPanel",this.bottomBar.getNextHighestDepth(),{buttonRenderer:"MappedButton",maxButtons:6,buttonInitializer:{disableConstraints:false,disabled:false,disableFocus:true,hiddenBackground:true}});
      _loc3_._y = this.navPanel._y + 28;
      _loc3_._x = this.navPanel._x;
   }
   function InitExtensions()
   {
      this.historyWindow.InitExtensions();
      this.bottomBar.playerInfo.Lock("R");
      this.bottomBar.positionBackground();
      var _loc3_ = this.historyWindow._x - this.brushWindow._x;
      var _loc2_ = {x:Stage.visibleRect.x / 2 + Stage.visibleRect.width - Stage.safeRect.x,y:0};
      this.globalToLocal(_loc2_);
      this.historyWindow._x = _loc2_.x - this.historyWindow._width;
      this.meshWindow._x = _loc2_.x - this.meshWindow._width;
      this.brushWindow._x = _loc2_.x - this.brushWindow._width - _loc3_;
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      this._bPS3Switch = a_bPS3Switch;
      this.bottomBar.setPlatform(a_platform,a_bPS3Switch);
      if(a_platform == 0)
      {
         this._exportHeadControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quicksave",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._importHeadControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quickload",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._importHeadControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sneak",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._exportHeadControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Toggle POV",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      this._acceptControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Ready Weapon",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      this._modifierControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sprint",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      this._upControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Up",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._downControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Down",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._leftControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Left",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._rightControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Right",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._udControl = [this._upControl,this._downControl];
      this._lrControl = [this._leftControl,this._rightControl];
      this._clearSculptControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Shout",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      var _loc6_ = Stage.visibleRect.x + Stage.safeRect.x;
      var _loc5_ = Stage.visibleRect.x + Stage.visibleRect.width - Stage.safeRect.x;
      this.bottomBar.positionElements(_loc6_,_loc5_);
      var _loc4_ = this.bottomBar.staticPanel;
      if(_loc4_)
      {
         _loc4_.setPlatform(a_platform,a_bPS3Switch);
         _loc4_._x = this.navPanel._x;
         _loc4_._y = this.navPanel._y + 28;
         _loc4_.clearButtons();
         if(_global.skse.plugins.CharGen)
         {
            if(this._platform == 0)
            {
               _loc4_.addButton({text:"$Export Head",controls:this._exportHeadControl}).addEventListener("click",this,"onExportHeadClicked");
               _loc4_.addButton({text:"$Import Head",controls:this._importHeadControl}).addEventListener("click",this,"onImportHeadClicked");
            }
            else
            {
               _loc4_.addButton({text:"$Import Head",controls:this._importHeadControl}).addEventListener("click",this,"onImportHeadClicked");
               _loc4_.addButton({text:"$Export Head",controls:this._exportHeadControl}).addEventListener("click",this,"onExportHeadClicked");
            }
            _loc4_.addButton({text:"$Clear Sculpt",controls:this._clearSculptControl}).addEventListener("click",this,"onClearSculptClicked");
         }
         _loc4_.updateButtons(true);
      }
      this.updateBottomBar();
   }
   function updateBottomBar()
   {
      this.navPanel.clearButtons();
      this.navPanel.addButton({text:"$Done",controls:this._acceptControl}).addEventListener("click",this._parent,"onDoneClicked");
      if(!this._secondary)
      {
         this.navPanel.addButton({text:"$Secondary",controls:this._modifierControl});
      }
      else
      {
         this.navPanel.addButton({text:"$Primary",controls:this._modifierControl});
      }
      this.navPanel.addButton({text:"$Brush",controls:this._udControl});
      this.navPanel.addButton({text:"$Property",controls:this._lrControl});
      this.navPanel.updateButtons(true);
   }
   function ShowAll(bShowAll, bRequestLoad, bRequestUnload)
   {
      this.ShowBottomBar(bShowAll);
      if(bShowAll)
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      if(bShowAll)
      {
         if(bRequestLoad)
         {
            this.wireframeDisplay.loadAssets();
            this.brushWindow.loadAssets();
            this.historyWindow.loadAssets();
            this.meshWindow.loadAssets();
         }
         this.ShowMeshWindow(true);
         this.ShowWireframe(true);
         this.ShowHistoryWindow(true);
         this.ShowBrushWindow(true);
         com.greensock.TweenLite.to(this.tempText,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         if(bRequestUnload)
         {
            this.meshWindow.unloadAssets();
            this.historyWindow.unloadAssets();
            this.brushWindow.unloadAssets();
            this.wireframeDisplay.unloadAssets();
         }
         this.ShowMeshWindow(false);
         this.ShowWireframe(false);
         this.ShowHistoryWindow(false);
         this.ShowBrushWindow(false);
         com.greensock.TweenLite.to(this.tempText,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      this.enabled = bShowAll;
   }
   function ShowMeshWindow(bShowWindow)
   {
      if(bShowWindow)
      {
         com.greensock.TweenLite.to(this.meshWindow,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.meshWindow,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowHistoryWindow(bShowWindow)
   {
      if(bShowWindow)
      {
         com.greensock.TweenLite.to(this.historyWindow,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.historyWindow,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowBrushWindow(bShowWindow)
   {
      if(bShowWindow)
      {
         com.greensock.TweenLite.to(this.brushWindow,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.brushWindow,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowWireframe(bShowWireframe)
   {
      if(bShowWireframe)
      {
         com.greensock.TweenLite.to(this.wireframeDisplay,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.wireframeDisplay,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
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
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      if(details.skseKeycode == this._modifierControl.keyCode)
      {
         this._secondary = details.value == "keyDown" || details.value == "keyHold";
         this.wireframeDisplay.secondary = this._secondary;
         this.updateBottomBar(true);
         return true;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(this.IsBoundKeyPressed(details,this._exportHeadControl,this._platform))
         {
            this.onExportHeadClicked();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._importHeadControl,this._platform))
         {
            this.onImportHeadClicked();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._clearSculptControl,this._platform))
         {
            this.onClearSculptClicked();
            return true;
         }
      }
      return this.brushWindow.handleInput(details,pathToFocus);
   }
   function hasAssets()
   {
      return this.wireframeDisplay.bLoadedAssets;
   }
   function unloadAssets()
   {
      this.meshWindow.unloadAssets();
      this.historyWindow.unloadAssets();
      this.brushWindow.unloadAssets();
      this.wireframeDisplay.unloadAssets();
   }
   function onBeginActivity(event)
   {
      this.brushWindow.bAllowBrushChange = false;
   }
   function onEndActivity(event)
   {
      this.brushWindow.bAllowBrushChange = true;
   }
   function onChangeBrush(event)
   {
      this.bottomBar.playerInfo.PlayerName.text = event.brushName;
   }
   function onExportHeadClicked()
   {
      var _loc3_ = new Date();
      var _loc4_ = "Head_" + (_loc3_.getMonth() + 1) + "-" + _loc3_.getDate() + "-" + _loc3_.getFullYear() + "_" + _loc3_.getHours() + "-" + _loc3_.getMinutes() + "-" + _loc3_.getSeconds();
      false;
      var _loc5_ = DialogTweenManager.open(_root,"FileViewerDialog",{_platform:this._platform,_bPS3Switch:this._bPS3Switch,titleText:"$Export Head",defaultText:_loc4_,path:"Data\\SKSE\\Plugins\\CharGen\\",patterns:["*.nif"],disableInput:false});
      _loc5_.addEventListener("accept",this,"onExportFile");
   }
   function onImportHeadClicked()
   {
      var _loc3_ = DialogTweenManager.open(_root,"FileViewerDialog",{_platform:this._platform,_bPS3Switch:this._bPS3Switch,titleText:"$Import Head",defaultText:"",path:"Data\\SKSE\\Plugins\\CharGen\\",patterns:["*.nif"],disableInput:true});
      _loc3_.addEventListener("accept",this,"onImportFile");
      _loc3_.addEventListener("dialogClosed",this,"onImportDialogClosed");
   }
   function onExportFile(event)
   {
      var _loc2_ = event.directoryPath + "\\" + event.input;
      _global.skse.plugins.CharGen.ExportHead(_loc2_);
   }
   function onImportFile(event)
   {
      this._importPath = event.directoryPath + "\\" + event.input;
      this._importName = event.input;
      this._importData = _global.skse.plugins.CharGen.ImportHead(this._importPath);
   }
   function onImportDialogClosed(event)
   {
      var _loc3_;
      if(this._importData.length > 0)
      {
         _loc3_ = DialogTweenManager.open(_root,"ImportDialog",{_platform:this._platform,_bPS3Switch:this._bPS3Switch,titleText:"$Import Part Matcher",importPath:this._importPath,source:this._importData,destination:this.meshWindow.GetInternalMeshes()});
         _loc3_.addEventListener("accept",this,"onImportHead");
      }
      this.dispatchEvent({type:"importedFile",success:this._importData.length > 0,name:this._importName});
   }
   function onImportHead(event)
   {
      _global.skse.plugins.CharGen.LoadImportedHead(event.matches);
   }
   function onClearSculptClicked()
   {
      var _loc4_ = this.meshWindow.GetInternalMeshes();
      var _loc5_ = new Array();
      var _loc3_ = 0;
      while(_loc3_ < _loc4_.length)
      {
         if(_loc4_[_loc3_].locked == false)
         {
            _loc5_.push(_loc4_[_loc3_].meshIndex);
         }
         _loc3_ = _loc3_ + 1;
      }
      _global.skse.plugins.CharGen.ClearSculptData(_loc5_);
      false;
   }
}
