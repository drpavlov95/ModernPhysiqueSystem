class SliderListEntry extends MovieClip
{
   var SliderInstance;
   var activeIndicator;
   var callbackName;
   var colorSquare;
   var entryClip;
   var entryObject;
   var eventData;
   var focusIndicator;
   var glowSquare;
   var internalCallback;
   var position;
   var proxyObject;
   var rateInterval;
   var selectIndicator;
   var sliderID;
   var textField;
   var trigger;
   var valueField;
   static var defaultTextColor = 16777215;
   static var disabledTextColor = 5000268;
   static var squareFillColor = 0;
   function SliderListEntry()
   {
      super();
      this.trigger.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
         }
      };
      this.trigger.onRollOut = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOut(this._parent.itemIndex);
         }
      };
      this.trigger.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPress(this._parent.itemIndex);
         }
      };
      this.activeIndicator.onPress = this.trigger.onPress;
      this.colorSquare.onRollOver = this.trigger.onRollOver;
      this.colorSquare.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPress(this._parent.itemIndex);
         }
      };
      this.glowSquare.onRollOver = this.trigger.onRollOver;
      this.glowSquare.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPressAux(this._parent.itemIndex,undefined,1);
         }
      };
   }
   function onTimerComplete()
   {
      skse.SendModEvent(this.eventData.event,this.eventData.callback,this.eventData.position);
      clearInterval(this.rateInterval);
      delete this.rateInterval;
   }
   function handleInput(details, pathToFocus)
   {
      var _loc4_ = this._parent;
      var _loc2_ = _loc4_.selectedEntry;
      if(_loc2_.type != RaceMenuDefines.ENTRY_TYPE_SLIDER)
      {
         return false;
      }
      var _loc3_;
      if(!this.SliderInstance.disabled)
      {
         _loc3_ = this.SliderInstance.handleInput(details,pathToFocus);
         if(_loc3_)
         {
            return true;
         }
      }
      return false;
   }
   function onLoad()
   {
      super.onLoad();
      this.SliderInstance.addEventListener("onWidgetLoad",this,"onSliderLoad");
   }
   function onSliderLoad(event)
   {
      this.setSlider(event.target,this.proxyObject);
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc9_ = a_entryObject == a_state.list.selectedEntry;
      var _loc7_ = a_state.activeEntry != undefined && a_entryObject == a_state.activeEntry;
      var _loc4_ = a_entryObject == a_state.focusEntry;
      var _loc10_ = a_state.focusEntry != undefined;
      var _loc0_;
      var _loc6_;
      var _loc8_;
      switch(a_entryObject.type)
      {
         case RaceMenuDefines.ENTRY_TYPE_RACE:
            var _temp_2 = this.valueField;
            var _temp_1 = "enabled";
            this.valueField._visible = _loc0_ = false;
            _temp_2[_temp_1] = _loc0_;
            var _temp_4 = this.SliderInstance;
            var _temp_3 = "enabled";
            this.SliderInstance._visible = _loc0_ = false;
            _temp_4[_temp_3] = _loc0_;
            var _temp_6 = this.colorSquare;
            var _temp_5 = "enabled";
            this.colorSquare._visible = _loc0_ = false;
            _temp_6[_temp_5] = _loc0_;
            var _temp_8 = this.glowSquare;
            var _temp_7 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_8[_temp_7] = _loc0_;
            a_entryObject.enabled = true;
            break;
         case RaceMenuDefines.ENTRY_TYPE_SLIDER:
            var _temp_10 = this.valueField;
            var _temp_9 = "_visible";
            this.valueField.enabled = _loc0_ = true;
            _temp_10[_temp_9] = _loc0_;
            var _temp_12 = this.SliderInstance;
            var _temp_11 = "_visible";
            this.SliderInstance.enabled = _loc0_ = true;
            _temp_12[_temp_11] = _loc0_;
            var _temp_14 = this.glowSquare;
            var _temp_13 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_14[_temp_13] = _loc0_;
            a_entryObject.enabled = !a_entryObject.hasColor() || a_entryObject.isColorEnabled();
            this.valueField.SetText((a_entryObject.position * 100 | 0) / 100);
            if(a_entryObject.hasColor())
            {
               _loc6_ = new Color(this.colorSquare.fill);
               _loc6_.setRGB(a_entryObject.fillColor & 0xFFFFFF);
               this.colorSquare.fill._alpha = (a_entryObject.fillColor >>> 24) / 255 * 100;
               this.colorSquare._visible = _global.skse != undefined;
               this.colorSquare.enabled = this.colorSquare._visible && a_entryObject.enabled;
            }
            else
            {
               var _temp_16 = this.colorSquare;
               var _temp_15 = "enabled";
               this.colorSquare._visible = _loc0_ = false;
               _temp_16[_temp_15] = _loc0_;
            }
            if(!this.SliderInstance.initialized)
            {
               this.proxyObject = a_entryObject;
            }
            else
            {
               this.setSlider(this.SliderInstance,a_entryObject);
            }
            break;
         case RaceMenuDefines.ENTRY_TYPE_WARPAINT:
         case RaceMenuDefines.ENTRY_TYPE_BODYPAINT:
         case RaceMenuDefines.ENTRY_TYPE_HANDPAINT:
         case RaceMenuDefines.ENTRY_TYPE_FEETPAINT:
         case RaceMenuDefines.ENTRY_TYPE_FACEPAINT:
            var _temp_18 = this.valueField;
            var _temp_17 = "_visible";
            this.valueField.enabled = _loc0_ = false;
            _temp_18[_temp_17] = _loc0_;
            var _temp_20 = this.SliderInstance;
            var _temp_19 = "_visible";
            this.SliderInstance.enabled = _loc0_ = false;
            _temp_20[_temp_19] = _loc0_;
            this.colorSquare._visible = a_entryObject.hasColor();
            this.glowSquare._visible = a_entryObject.hasGlow();
            a_entryObject.enabled = a_entryObject.isColorEnabled();
            this.colorSquare.enabled = this.colorSquare._visible && a_entryObject.enabled;
            this.glowSquare.enabled = this.glowSquare._visible && a_entryObject.enabled;
            _loc6_ = new Color(this.colorSquare.fill);
            _loc6_.setRGB(a_entryObject.fillColor & 0xFFFFFF);
            this.colorSquare.fill._alpha = (a_entryObject.fillColor >>> 24) / 255 * 100;
            _loc8_ = new Color(this.glowSquare.fill);
            _loc8_.setRGB(a_entryObject.glowColor & 0xFFFFFF);
            this.glowSquare.fill._alpha = (a_entryObject.glowColor >>> 24) / 255 * 100;
      }
      this.enabled = a_entryObject.enabled;
      if(_loc10_ && !_loc4_ || !this.enabled)
      {
         this.textField.textColor = SliderListEntry.disabledTextColor;
         this.valueField.textColor = SliderListEntry.disabledTextColor;
         this.SliderInstance._alpha = 40;
         this.colorSquare._alpha = 40;
         this.glowSquare._alpha = 40;
      }
      else
      {
         this.textField.textColor = SliderListEntry.defaultTextColor;
         this.valueField.textColor = SliderListEntry.defaultTextColor;
         this.SliderInstance._alpha = 100;
         this.colorSquare._alpha = 100;
         this.glowSquare._alpha = 100;
      }
      if(this.textField != undefined)
      {
         this.textField.autoSize = !a_entryObject.align ? "left" : a_entryObject.align;
         this.textField.SetText(!a_entryObject.text ? " " : a_entryObject.text);
      }
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc9_ && !_loc4_;
      }
      if(this.activeIndicator != undefined)
      {
         this.activeIndicator._visible = _loc7_;
         this.activeIndicator._x = this.textField._x + this.textField._width + 5;
      }
      if(this.focusIndicator != undefined)
      {
         this.focusIndicator._visible = _loc4_;
      }
   }
   function updatePosition(a_position)
   {
      this.SliderInstance.position = a_position;
      this.SliderInstance.changedCallback();
   }
   function setSlider(slider, a_entryObject)
   {
      if(!slider)
      {
         return undefined;
      }
      slider.minimum = a_entryObject.sliderMin;
      slider.maximum = a_entryObject.sliderMax;
      slider.snapInterval = a_entryObject.interval;
      slider.position = a_entryObject.position;
      slider.callbackName = a_entryObject.callbackName;
      slider.sliderID = a_entryObject.sliderID;
      slider.internalCallback = a_entryObject.internalCallback;
      slider.entryObject = a_entryObject;
      slider.entryClip = this;
      slider.changedCallback = function()
      {
         if(this.faceMapGuardRestoring)
         {
            return undefined;
         }
         if(this.callbackName == "ChangeSex")
         {
            var faceMapReady = false;
            if(_global.skse && _global.skse.plugins && _global.skse.plugins.ModernPhysiqueSystem && _global.skse.plugins.ModernPhysiqueSystem.PrepareFaceMapForSex)
            {
               faceMapReady = Boolean(_global.skse.plugins.ModernPhysiqueSystem.PrepareFaceMapForSex(this.position));
            }
            if(!faceMapReady)
            {
               // Do not let the native callback run against an unverified
               // race-keyed SKEE list. Keep the UI on the actor's old sex.
               this.faceMapGuardRestoring = true;
               this.position = this.entryObject.position;
               delete this.faceMapGuardRestoring;
               this._parent.valueField.SetText((this.position * 100 | 0) / 100);
               return undefined;
            }
         }
         gfx.io.GameDelegate.call(this.callbackName,[this.position,this.sliderID]);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
         this.entryClip.eventData = {event:_global.eventPrefix + "SliderChange",callback:this.callbackName,position:this.position};
         if(!this.entryClip.rateInterval)
         {
            this.entryClip.rateInterval = setInterval(this.entryClip,"onTimerComplete",100);
         }
         this.entryObject.position = this.position;
         this._parent.valueField.SetText((this.position * 100 | 0) / 100);
         if(this.internalCallback)
         {
            this.internalCallback();
         }
      };
      slider.addEventListener("change",slider,"changedCallback");
      slider.disabled = !a_entryObject.enabled;
   }
}
