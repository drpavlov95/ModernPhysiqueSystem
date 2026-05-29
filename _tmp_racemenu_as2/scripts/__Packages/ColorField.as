class ColorField extends MovieClip
{
   var _acceptButton;
   var _cancelButton;
   var _currentColor;
   var _currentSlider;
   var _loadColorButton;
   var _platform;
   var _saveColorButton;
   var _switchModeButton;
   var bCanSwitchMode;
   var bNoAlpha;
   var buttonPanel;
   var colorSelector;
   var colorText;
   var currentSelectionLeft;
   var currentSelectionRight;
   var dispatchEvent;
   var entry;
   var hexCode;
   var mode;
   var presetPanel;
   var savedColor;
   var _sliderCount = 4;
   function ColorField()
   {
      super();
      this._currentColor = 0;
      this._currentSlider = 0;
      gfx.events.EventDispatcher.initialize(this);
      this.colorText.textAutoSize = "shrink";
   }
   function onLoad()
   {
      this.colorSelector.addEventListener("changeColor",this,"onSliderChange");
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      var _loc3_ = false;
      var _loc4_;
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Activate",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onAccept();
            _loc3_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.TAB || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Cancel",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onCancel();
            _loc3_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.DOWN)
         {
            this._currentSlider = this._currentSlider + 1;
            if(this._currentSlider > this._sliderCount - 1)
            {
               this._currentSlider = 0;
            }
            this.UpdateSelection();
            gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
            _loc3_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.UP)
         {
            this._currentSlider = this._currentSlider - 1;
            if(this._currentSlider < 0)
            {
               this._currentSlider = this._sliderCount - 1;
            }
            this.UpdateSelection();
            gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
            _loc3_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.LEFT || details.navEquivalent == gfx.ui.NavigationCode.RIGHT || details.navEquivalent == gfx.ui.NavigationCode.HOME || details.navEquivalent == gfx.ui.NavigationCode.END || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_L2 || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_R2 || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_L1 || details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_R1)
         {
            _loc4_ = null;
            switch(this._currentSlider)
            {
               case 0:
                  _loc4_ = this.colorSelector.hSlider;
                  break;
               case 1:
                  _loc4_ = this.colorSelector.sSlider;
                  break;
               case 2:
                  _loc4_ = this.colorSelector.vSlider;
                  break;
               case 3:
                  _loc4_ = this.colorSelector.aSlider;
            }
            _loc3_ = _loc4_.handleInput(details,pathToFocus);
         }
         else if(this.bCanSwitchMode == true && this.IsBoundKeyPressed(details,this._switchModeButton,this._platform))
         {
            this.onSwitchMode();
            _loc3_ = true;
         }
         else if(this.IsBoundKeyPressed(details,this._loadColorButton,this._platform))
         {
            this.onLoadColor();
            _loc3_ = true;
         }
         else if(this.IsBoundKeyPressed(details,this._saveColorButton,this._platform))
         {
            this.onSaveColor();
            _loc3_ = true;
         }
      }
      return _loc3_;
   }
   function SetupButtons()
   {
      this.buttonPanel.clearButtons();
      this.presetPanel.clearButtons();
      var _loc3_ = this.buttonPanel.addButton({text:"$Accept",controls:this._acceptButton});
      _loc3_.addEventListener("click",this,"onAccept");
      var _loc4_ = this.buttonPanel.addButton({text:"$Cancel",controls:this._cancelButton});
      _loc4_.addEventListener("click",this,"onCancel");
      var _loc2_;
      if(this.bCanSwitchMode)
      {
         _loc2_ = this.buttonPanel.addButton({text:this.getModeText(),controls:this._switchModeButton});
         _loc2_.addEventListener("click",this,"onSwitchMode");
      }
      var _loc5_ = this.presetPanel.addButton({text:"$Load Color",controls:this._loadColorButton});
      _loc5_.addEventListener("click",this,"onLoadColor");
      var _loc6_ = this.presetPanel.addButton({text:"$Save Color",controls:this._saveColorButton});
      _loc6_.addEventListener("click",this,"onSaveColor");
      this.buttonPanel.updateButtons(true);
      this.presetPanel.updateButtons(true);
   }
   function UpdateSelection()
   {
      this.currentSelectionLeft.gotoAndStop(this._currentSlider + 1);
      this.currentSelectionRight.gotoAndStop(this._currentSlider + 1);
   }
   function ResetSlider()
   {
      this._currentSlider = 0;
      this.UpdateSelection();
   }
   function hexToStr(a_AARRGGBB, a_prefix)
   {
      var _loc2_ = a_AARRGGBB.toString(16).toUpperCase();
      var _loc3_ = "";
      var _loc1_ = _loc2_.length;
      while(_loc1_ < 8)
      {
         _loc3_ += "0";
         _loc1_ = _loc1_ + 1;
      }
      _loc2_ = (!a_prefix ? "" : "0x") + _loc3_ + _loc2_;
      return _loc2_;
   }
   function set initParams(a_object)
   {
      for(var _loc3_ in a_object)
      {
         this[_loc3_] = a_object[_loc3_];
      }
      this.disableAlpha(a_object.bNoAlpha);
      this.SetupButtons();
   }
   function disableAlpha(a_disable)
   {
      if(a_disable)
      {
         this._sliderCount = 3;
         this.colorSelector.aSlider._visible = false;
         this.colorSelector.aSlider.enabled = false;
         this.colorSelector._alphaValue = 255;
         if(this._currentSlider == 3)
         {
            this._currentSlider = 0;
            this.UpdateSelection();
            this.updateHexCode();
         }
      }
      else
      {
         this._sliderCount = 4;
         this.colorSelector.aSlider._visible = true;
         this.colorSelector.aSlider.enabled = true;
      }
      this.bNoAlpha = a_disable;
   }
   function getColor()
   {
      return this._currentColor;
   }
   function setText(a_text)
   {
      this.colorText.text = a_text;
   }
   function updateHexCode()
   {
      var _loc5_ = this.colorSelector.getAlpha() >>> 0;
      var _loc2_ = this.colorSelector.getColor() >>> 16;
      var _loc4_ = this.colorSelector.getColor() >>> 8 & 0xFF;
      var _loc3_ = this.colorSelector.getColor() & 0xFF;
      this.hexCode.text = this.hexToStr(this.colorSelector.getColor() | this.colorSelector.getAlpha() << 24,false) + " (A:" + _loc5_ + " R:" + _loc2_ + " G:" + _loc4_ + " B:" + _loc3_ + ")";
   }
   function setColor(a_color, bUpdate)
   {
      this._currentColor = a_color;
      this.colorSelector.setColor(this._currentColor & 0xFFFFFF,this._currentColor >>> 24);
      this.updateHexCode();
   }
   function updateButtons(bInstant)
   {
      this.buttonPanel.updateButtons(bInstant);
      this.presetPanel.updateButtons(bInstant);
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      if(a_platform == 0)
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = {name:"Tween Menu",context:skyui.defines.Input.CONTEXT_GAMEPLAY};
         this._switchModeButton = skyui.defines.Input.Wait;
         this._saveColorButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quicksave",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._loadColorButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Quickload",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = skyui.defines.Input.Cancel;
         this._switchModeButton = skyui.defines.Input.YButton;
         this._saveColorButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Toggle POV",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._loadColorButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sneak",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      this.buttonPanel.setPlatform(a_platform,a_bPS3Switch);
      this.presetPanel.setPlatform(a_platform,a_bPS3Switch);
   }
   function onSwitchMode()
   {
      if(this.bCanSwitchMode)
      {
         this.dispatchEvent({type:"changeColor",entry:this.entry,mode:this.mode,color:this.colorSelector.getColor() | this.colorSelector.getAlpha() << 24,apply:true,closeField:false});
         this.dispatchEvent({type:"switchMode",mode:this.mode});
         this.SetupButtons();
      }
   }
   function getModeText()
   {
      switch(this.mode)
      {
         case "tint":
            return "$Glow";
         case "glow":
            return "$Tint";
         default:
            return "";
      }
   }
   function onLoadColor()
   {
      var _loc2_ = 0;
      if(this.bNoAlpha)
      {
         _loc2_ = this.savedColor & 0xFFFFFF | 0xFF000000;
      }
      else
      {
         _loc2_ = this.savedColor;
      }
      this.colorSelector.setColor(_loc2_ & 0xFFFFFF,_loc2_ >>> 24);
      this.updateHexCode();
      this.dispatchEvent({type:"changeColor",entry:this.entry,mode:this.mode,color:this.colorSelector.getColor() | this.colorSelector.getAlpha() << 24,apply:false,closeField:false});
   }
   function onSaveColor()
   {
      this.savedColor = this.colorSelector.getColor() | this.colorSelector.getAlpha() << 24;
      this.dispatchEvent({type:"saveColor",color:this.savedColor});
   }
   function onSliderChange(event)
   {
      this.dispatchEvent({type:"changeColor",entry:this.entry,mode:this.mode,color:event.color | event.alpha << 24,apply:false,closeField:false});
      this.updateHexCode();
   }
   function onAccept()
   {
      this.dispatchEvent({type:"changeColor",entry:this.entry,mode:this.mode,color:this.colorSelector.getColor() | this.colorSelector.getAlpha() << 24,apply:true,closeField:true});
   }
   function onCancel()
   {
      this.dispatchEvent({type:"changeColor",entry:this.entry,mode:this.mode,color:this._currentColor >> 0,apply:true,closeField:true});
   }
}
