class PresetEditor extends MovieClip
{
   var _acceptControl;
   var _activateControl;
   var _bPS3Switch;
   var _cancelControl;
   var _loadPresetControl;
   var _panelX;
   var _platform;
   var _savePresetControl;
   var bottomBar;
   var dispatchEvent;
   var itemList;
   var navPanel;
   var presetPanel;
   var ITEMLIST_HIDDEN_X = -478;
   var BOTTOMBAR_SHOWN_Y = 645;
   var BOTTOMBAR_HIDDEN_Y = 745;
   function PresetEditor()
   {
      super();
      Mouse.addListener(this);
      gfx.events.EventDispatcher.initialize(this);
      this.navPanel = this.bottomBar.buttonPanel;
      this.itemList = this.presetPanel.itemList;
      this.itemList.disableSelection = this.itemList.disableInput = false;
      this.bottomBar._y = this.BOTTOMBAR_HIDDEN_Y;
   }
   function onLoad()
   {
      super.onLoad();
      this.bottomBar.hidePlayerInfo();
   }
   function InitExtensions()
   {
      this.presetPanel.Lock("L");
      this._panelX = this.presetPanel._x;
      this.presetPanel._x = this.ITEMLIST_HIDDEN_X;
      this.bottomBar.positionBackground();
      this.itemList.listEnumeration = new skyui.components.list.BasicEnumeration(this.itemList.entryList);
      var _loc3_;
      if(!_global.skse.plugins.CharGen)
      {
         _loc3_ = {type:RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT,text:"CharGen Presets Unavailable",filterFlag:1,enabled:true};
         this.itemList.entryList.push(_loc3_);
      }
      gfx.managers.FocusHandler.instance.setFocus(this.itemList,0);
      this.itemList.requestInvalidate();
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      this._bPS3Switch = a_bPS3Switch;
      this.bottomBar.setPlatform(a_platform,a_bPS3Switch);
      this._activateControl = skyui.defines.Input.Accept;
      if(this._platform == 0)
      {
         this._cancelControl = {name:"Tween Menu",context:skyui.defines.Input.CONTEXT_GAMEPLAY};
         this._savePresetControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quicksave",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._loadPresetControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quickload",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._cancelControl = skyui.defines.Input.Cancel;
         this._loadPresetControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sneak",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._savePresetControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Toggle POV",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      this._acceptControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Ready Weapon",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      var _loc4_ = Stage.visibleRect.x + Stage.safeRect.x;
      var _loc3_ = Stage.visibleRect.x + Stage.visibleRect.width - Stage.safeRect.x;
      this.bottomBar.positionElements(_loc4_,_loc3_);
      this.updateBottomBar();
   }
   function updateBottomBar()
   {
      this.navPanel.clearButtons();
      this.navPanel.addButton({text:"$Done",controls:this._acceptControl}).addEventListener("click",this._parent,"onDoneClicked");
      if(this._platform == 0)
      {
         this.navPanel.addButton({text:"$Save Preset",controls:this._savePresetControl}).addEventListener("click",this,"onSavePresetClicked");
         this.navPanel.addButton({text:"$Load Preset",controls:this._loadPresetControl}).addEventListener("click",this,"onLoadPresetClicked");
      }
      else
      {
         this.navPanel.addButton({text:"$Load Preset",controls:this._loadPresetControl}).addEventListener("click",this,"onLoadPresetClicked");
         this.navPanel.addButton({text:"$Save Preset",controls:this._savePresetControl}).addEventListener("click",this,"onSavePresetClicked");
      }
      this.navPanel.updateButtons(true);
   }
   function ShowPanel(bShowPanel)
   {
      if(bShowPanel)
      {
         this.itemList.disableSelection = this.itemList.disableInput = false;
         com.greensock.TweenLite.to(this.presetPanel,0.5,{autoAlpha:100,_x:this._panelX,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         this.itemList.disableSelection = this.itemList.disableInput = true;
         com.greensock.TweenLite.to(this.presetPanel,0.5,{autoAlpha:0,_x:this.ITEMLIST_HIDDEN_X,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function ShowAll(bShowAll)
   {
      this.ShowPanel(bShowAll);
      this.ShowBottomBar(bShowAll);
      if(bShowAll)
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      this.enabled = bShowAll;
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
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(this.IsBoundKeyPressed(details,this._loadPresetControl,this._platform) && !this._parent.bTextEntryMode)
         {
            this.onLoadPresetClicked();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._savePresetControl,this._platform) && !this._parent.bTextEntryMode)
         {
            this.onSavePresetClicked();
            return true;
         }
      }
      if(this.itemList.handleInput(details,pathToFocus))
      {
         return true;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.RIGHT)
         {
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.LEFT)
         {
            return true;
         }
      }
      return false;
   }
   function readPreset(a_path, a_json)
   {
      var _loc3_ = new Object();
      var _loc18_ = _global.skse.plugins.CharGen.ReadPreset(a_path,_loc3_,a_json);
      var _loc7_;
      var _loc9_;
      var _loc8_;
      var _loc17_;
      var _loc5_;
      var _loc4_;
      var _loc6_;
      if(_loc18_ == false)
      {
         this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_HEADER,text:"$Mods",filterFlag:1,enabled:true});
         _loc7_ = 0;
         while(_loc7_ < _loc3_.mods.length)
         {
            _loc9_ = _loc3_.mods[_loc7_];
            _loc8_ = 1611029;
            if(_loc9_.loadedIndex == 255)
            {
               _loc8_ = 16711680;
            }
            this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT,text:_loc9_.name,filterFlag:1,textFieldColor:_loc8_,enabled:true});
            _loc7_ = _loc7_ + 1;
         }
         this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_HEADER,text:"$Head Parts",filterFlag:1,enabled:true});
         _loc7_ = 0;
         while(_loc7_ < _loc3_.headParts.length)
         {
            this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT,text:_loc3_.headParts[_loc7_],filterFlag:1,enabled:true});
            _loc7_ = _loc7_ + 1;
         }
         this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_HEADER,text:"$Colors",filterFlag:1,enabled:true});
         _loc17_ = {type:RaceMenuDefines.PRESET_ENTRY_TYPE_COLOR,text:_loc3_.hair.name,fillColor:0xFF000000 | _loc3_.hair.value,filterFlag:1,enabled:true};
         _loc17_.isColorEnabled = function()
         {
            return true;
         };
         _loc17_.hasColor = function()
         {
            return true;
         };
         _loc17_.hasGlow = function()
         {
            return false;
         };
         this.itemList.entryList.push(_loc17_);
         _loc7_ = 0;
         while(_loc7_ < _loc3_.tints.length)
         {
            _loc5_ = _loc3_.tints[_loc7_];
            if(_loc5_.color >>> 24 > 0)
            {
               _loc4_ = {type:RaceMenuDefines.PRESET_ENTRY_TYPE_COLOR,text:this.stripTexturePath(_loc5_.texture),fillColor:_loc5_.color,filterFlag:1,enabled:true};
               _loc4_.isColorEnabled = function()
               {
                  return true;
               };
               _loc4_.hasColor = function()
               {
                  return true;
               };
               _loc4_.hasGlow = function()
               {
                  return false;
               };
               this.itemList.entryList.push(_loc4_);
            }
            _loc7_ = _loc7_ + 1;
         }
         this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_HEADER,text:"$Sliders",filterFlag:1,enabled:true});
         this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT_VALUE,text:_loc3_.weight.name,position:_loc3_.weight.value,filterFlag:1,enabled:true});
         _loc7_ = 0;
         while(_loc7_ < _loc3_.morphs.length)
         {
            _loc6_ = _loc3_.morphs[_loc7_];
            if(_loc6_.name)
            {
               this.itemList.entryList.push({type:RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT_VALUE,text:_loc6_.name,position:_loc6_.value,filterFlag:1,enabled:true});
            }
            _loc7_ = _loc7_ + 1;
         }
      }
   }
   function stripTexturePath(a_texture)
   {
      var _loc3_ = -1;
      var _loc1_ = a_texture.length - 1;
      while(_loc1_ > 0)
      {
         if(a_texture.charAt(_loc1_) == "\\" || a_texture.charAt(_loc1_) == "/")
         {
            _loc3_ = _loc1_;
            break;
         }
         _loc1_ = _loc1_ - 1;
      }
      var _loc4_ = a_texture.indexOf(".dds");
      if(_loc4_ == -1)
      {
         _loc4_ = a_texture.length;
      }
      return a_texture.substring(_loc3_ + 1,_loc4_);
   }
   function onSavePresetClicked()
   {
      var _loc3_ = new Date();
      var _loc4_ = "Preset_" + (_loc3_.getMonth() + 1) + "-" + _loc3_.getDate() + "-" + _loc3_.getFullYear() + "_" + _loc3_.getHours() + "-" + _loc3_.getMinutes() + "-" + _loc3_.getSeconds();
      false;
      var _loc5_ = DialogTweenManager.open(_root,"FileViewerDialog",{_platform:this._platform,_bPS3Switch:this._bPS3Switch,titleText:"$Save preset file",defaultText:_loc4_,path:"Data\\SKSE\\Plugins\\CharGen\\Presets\\",patterns:["*.jslot","*.slot"],disableInput:false});
      _loc5_.addEventListener("accept",this,"onSaveFile");
   }
   function onLoadPresetClicked()
   {
      var _loc3_ = DialogTweenManager.open(_root,"FileViewerDialog",{_platform:this._platform,_bPS3Switch:this._bPS3Switch,titleText:"$Select preset file",defaultText:"",path:"Data\\SKSE\\Plugins\\CharGen\\Presets\\",patterns:["*.jslot","*.slot"],disableInput:true});
      _loc3_.addEventListener("selectionChanged",this,"onSelectFile");
      _loc3_.addEventListener("accept",this,"onLoadFile");
   }
   function onSelectFile(event)
   {
      this.itemList.entryList.splice(0,this.itemList.entryList.length);
      var _loc2_;
      var _loc5_;
      var _loc4_;
      if(!event.directory)
      {
         _loc2_ = event.directoryPath + "\\" + event.input;
         _loc5_ = _loc2_.substring(_loc2_.lastIndexOf("."),_loc2_.length);
         _loc4_ = _loc5_ != ".jslot" ? false : true;
         this.readPreset(_loc2_,_loc4_);
      }
      this.itemList.requestInvalidate();
   }
   function onSaveFile(event)
   {
      var _loc3_ = event.directoryPath + "\\" + event.input;
      if(_loc3_.lastIndexOf(".") == -1)
      {
         _loc3_ += ".jslot";
      }
      var _loc4_ = _global.skse.plugins.CharGen.SavePreset(_loc3_,true);
      this.dispatchEvent({type:"savedPreset",name:event.input,success:!_loc4_});
      if(!_loc4_)
      {
         this.itemList.entryList.splice(0,this.itemList.entryList.length);
         this.readPreset(_loc3_,true);
         this.itemList.requestInvalidate();
      }
   }
   function onLoadFile(event)
   {
      var _loc3_ = event.directoryPath + "\\" + event.input;
      var _loc8_;
      var _loc7_;
      var _loc4_;
      var _loc6_;
      if(_loc3_)
      {
         _loc8_ = _loc3_.substring(_loc3_.lastIndexOf("."),_loc3_.length);
         _loc7_ = _loc8_ != ".jslot" ? false : true;
         _loc4_ = new Object();
         this._parent.loadingIcon._visible = true;
         _loc6_ = _global.skse.plugins.CharGen.LoadPreset(_loc3_,_loc4_,_loc7_);
         this._parent.loadingIcon._visible = false;
         this.dispatchEvent({type:"loadedPreset",name:event.input,success:!_loc6_,data:_loc4_});
      }
   }
}
