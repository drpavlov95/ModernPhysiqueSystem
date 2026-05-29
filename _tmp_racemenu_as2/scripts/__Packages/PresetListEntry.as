class PresetListEntry extends MovieClip
{
   var SliderInstance;
   var activeIndicator;
   var callbackName;
   var centerField;
   var colorSquare;
   var entryObject;
   var focusIndicator;
   var glowSquare;
   var internalCallback;
   var position;
   var proxyObject;
   var selectIndicator;
   var sliderID;
   var textField;
   var trigger;
   var valueField;
   static var defaultTextColor = 16777215;
   static var disabledTextColor = 5000268;
   static var squareFillColor = 0;
   function PresetListEntry()
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
         case RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT:
            var _temp_2 = this.textField;
            var _temp_1 = "enabled";
            this.textField._visible = _loc0_ = true;
            _temp_2[_temp_1] = _loc0_;
            var _temp_4 = this.valueField;
            var _temp_3 = "enabled";
            this.valueField._visible = _loc0_ = false;
            _temp_4[_temp_3] = _loc0_;
            var _temp_6 = this.SliderInstance;
            var _temp_5 = "enabled";
            this.SliderInstance._visible = _loc0_ = false;
            _temp_6[_temp_5] = _loc0_;
            var _temp_8 = this.colorSquare;
            var _temp_7 = "enabled";
            this.colorSquare._visible = _loc0_ = false;
            _temp_8[_temp_7] = _loc0_;
            var _temp_10 = this.glowSquare;
            var _temp_9 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_10[_temp_9] = _loc0_;
            var _temp_12 = this.centerField;
            var _temp_11 = "enabled";
            this.centerField._visible = _loc0_ = false;
            _temp_12[_temp_11] = _loc0_;
            a_entryObject.sliderVisible = false;
            a_entryObject.enabled = true;
            break;
         case RaceMenuDefines.PRESET_ENTRY_TYPE_TEXT_VALUE:
            var _temp_14 = this.textField;
            var _temp_13 = "enabled";
            this.textField._visible = _loc0_ = true;
            _temp_14[_temp_13] = _loc0_;
            var _temp_16 = this.valueField;
            var _temp_15 = "enabled";
            this.valueField._visible = _loc0_ = true;
            _temp_16[_temp_15] = _loc0_;
            var _temp_18 = this.SliderInstance;
            var _temp_17 = "enabled";
            this.SliderInstance._visible = _loc0_ = false;
            _temp_18[_temp_17] = _loc0_;
            var _temp_20 = this.colorSquare;
            var _temp_19 = "enabled";
            this.colorSquare._visible = _loc0_ = false;
            _temp_20[_temp_19] = _loc0_;
            var _temp_22 = this.glowSquare;
            var _temp_21 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_22[_temp_21] = _loc0_;
            var _temp_24 = this.centerField;
            var _temp_23 = "enabled";
            this.centerField._visible = _loc0_ = false;
            _temp_24[_temp_23] = _loc0_;
            this.valueField.SetText((a_entryObject.position * 100 | 0) / 100);
            a_entryObject.sliderVisible = false;
            a_entryObject.enabled = true;
            break;
         case RaceMenuDefines.PRESET_ENTRY_TYPE_SLIDER:
            var _temp_26 = this.textField;
            var _temp_25 = "enabled";
            this.textField._visible = _loc0_ = true;
            _temp_26[_temp_25] = _loc0_;
            var _temp_28 = this.valueField;
            var _temp_27 = "_visible";
            this.valueField.enabled = _loc0_ = true;
            _temp_28[_temp_27] = _loc0_;
            var _temp_30 = this.SliderInstance;
            var _temp_29 = "_visible";
            this.SliderInstance.enabled = _loc0_ = true;
            _temp_30[_temp_29] = _loc0_;
            var _temp_32 = this.glowSquare;
            var _temp_31 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_32[_temp_31] = _loc0_;
            var _temp_34 = this.centerField;
            var _temp_33 = "enabled";
            this.centerField._visible = _loc0_ = false;
            _temp_34[_temp_33] = _loc0_;
            a_entryObject.enabled = !a_entryObject.hasColor() || a_entryObject.isColorEnabled();
            this.valueField.SetText((a_entryObject.position * 100 | 0) / 100);
            if(a_entryObject.hasColor())
            {
               _loc6_ = new Color(this.colorSquare.fill);
               _loc6_.setRGB(a_entryObject.fillColor & 0xFFFFFF);
               this.colorSquare.fill._alpha = (a_entryObject.fillColor >>> 24) / 255 * 100;
               var _temp_36 = this.colorSquare;
               var _temp_35 = "enabled";
               this.colorSquare._visible = _loc0_ = _global.skse != undefined;
               _temp_36[_temp_35] = _loc0_;
            }
            else
            {
               var _temp_38 = this.colorSquare;
               var _temp_37 = "enabled";
               this.colorSquare._visible = _loc0_ = false;
               _temp_38[_temp_37] = _loc0_;
            }
            a_entryObject.sliderVisible = true;
            if(!this.SliderInstance.initialized)
            {
               this.proxyObject = a_entryObject;
            }
            else
            {
               this.setSlider(this.SliderInstance,a_entryObject);
            }
            break;
         case RaceMenuDefines.PRESET_ENTRY_TYPE_COLOR:
            var _temp_40 = this.textField;
            var _temp_39 = "enabled";
            this.textField._visible = _loc0_ = true;
            _temp_40[_temp_39] = _loc0_;
            var _temp_42 = this.valueField;
            var _temp_41 = "_visible";
            this.valueField.enabled = _loc0_ = false;
            _temp_42[_temp_41] = _loc0_;
            var _temp_44 = this.SliderInstance;
            var _temp_43 = "_visible";
            this.SliderInstance.enabled = _loc0_ = false;
            _temp_44[_temp_43] = _loc0_;
            var _temp_46 = this.centerField;
            var _temp_45 = "enabled";
            this.centerField._visible = _loc0_ = false;
            _temp_46[_temp_45] = _loc0_;
            var _temp_48 = this.colorSquare;
            var _temp_47 = "_visible";
            this.colorSquare.enabled = _loc0_ = a_entryObject.hasColor();
            _temp_48[_temp_47] = _loc0_;
            var _temp_50 = this.glowSquare;
            var _temp_49 = "_visible";
            this.glowSquare.enabled = _loc0_ = a_entryObject.hasGlow();
            _temp_50[_temp_49] = _loc0_;
            a_entryObject.enabled = a_entryObject.isColorEnabled();
            _loc6_ = new Color(this.colorSquare.fill);
            _loc6_.setRGB(a_entryObject.fillColor & 0xFFFFFF);
            this.colorSquare.fill._alpha = (a_entryObject.fillColor >>> 24) / 255 * 100;
            _loc8_ = new Color(this.glowSquare.fill);
            _loc8_.setRGB(a_entryObject.glowColor & 0xFFFFFF);
            this.glowSquare.fill._alpha = (a_entryObject.glowColor >>> 24) / 255 * 100;
            break;
         case RaceMenuDefines.PRESET_ENTRY_TYPE_HEADER:
            var _temp_52 = this.textField;
            var _temp_51 = "enabled";
            this.textField._visible = _loc0_ = false;
            _temp_52[_temp_51] = _loc0_;
            var _temp_54 = this.valueField;
            var _temp_53 = "enabled";
            this.valueField._visible = _loc0_ = false;
            _temp_54[_temp_53] = _loc0_;
            var _temp_56 = this.SliderInstance;
            var _temp_55 = "enabled";
            this.SliderInstance._visible = _loc0_ = false;
            _temp_56[_temp_55] = _loc0_;
            var _temp_58 = this.colorSquare;
            var _temp_57 = "enabled";
            this.colorSquare._visible = _loc0_ = false;
            _temp_58[_temp_57] = _loc0_;
            var _temp_60 = this.glowSquare;
            var _temp_59 = "enabled";
            this.glowSquare._visible = _loc0_ = false;
            _temp_60[_temp_59] = _loc0_;
            var _temp_62 = this.centerField;
            var _temp_61 = "enabled";
            this.centerField._visible = _loc0_ = true;
            _temp_62[_temp_61] = _loc0_;
            this.centerField.SetText(!a_entryObject.text ? " " : a_entryObject.text);
            a_entryObject.sliderVisible = false;
            a_entryObject.enabled = false;
      }
      this.enabled = a_entryObject.enabled;
      if(_loc10_ && !_loc4_ || !this.enabled)
      {
         this.textField.textColor = PresetListEntry.disabledTextColor;
         this.valueField.textColor = PresetListEntry.disabledTextColor;
         this.SliderInstance._alpha = 40;
         this.colorSquare._alpha = 40;
         this.glowSquare._alpha = 40;
      }
      else
      {
         if(!a_entryObject.textFieldColor)
         {
            this.textField.textColor = PresetListEntry.defaultTextColor;
         }
         else
         {
            this.textField.textColor = a_entryObject.textFieldColor;
         }
         if(!a_entryObject.valueFieldColor)
         {
            this.valueField.textColor = PresetListEntry.defaultTextColor;
         }
         else
         {
            this.valueField.textColor = a_entryObject.valueFieldColor;
         }
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
      slider.changedCallback = function()
      {
         gfx.io.GameDelegate.call(this.callbackName,[this.position,this.sliderID]);
         gfx.io.GameDelegate.call("PlaySound",["UIMenuFocus"]);
         skse.SendModEvent(_global.eventPrefix + "SliderChange",this.callbackName,this.position);
         this.entryObject.position = this.position;
         this._parent.valueField.SetText((this.position * 100 | 0) / 100);
         if(this.internalCallback)
         {
            this.internalCallback();
         }
      };
      slider.addEventListener("change",slider,"changedCallback");
      slider._visible = a_entryObject.sliderVisible;
      slider.disabled = !a_entryObject.sliderEnabled;
   }
}
