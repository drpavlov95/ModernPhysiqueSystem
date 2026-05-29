class BrushListEntry extends MovieClip
{
   var SliderInstance;
   var entryObject;
   var internalCallback;
   var position;
   var proxyObject;
   var selectIndicator;
   var textField;
   var trigger;
   var valueField;
   static var defaultTextColor = 16777215;
   static var disabledTextColor = 5000268;
   function BrushListEntry()
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
   function handleInput(details, pathToFocus)
   {
      var _loc3_ = this._parent;
      var _loc4_ = _loc3_.selectedEntry;
      var _loc2_;
      if(!this.SliderInstance.disabled)
      {
         _loc2_ = this.SliderInstance.handleInput(details,pathToFocus);
         if(_loc2_)
         {
            return true;
         }
      }
      return false;
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc3_ = a_entryObject == a_state.list.selectedEntry;
      if(this.textField != undefined)
      {
         this.textField.autoSize = !a_entryObject.align ? "center" : a_entryObject.align;
         this.textField.SetText(!a_entryObject.text ? " " : a_entryObject.text);
      }
      if(this.valueField != undefined)
      {
         this.valueField._x = this.textField._x + this.textField._width / 2 + this.textField.textWidth / 2 + 5;
         this.valueField.autoSize = "left";
         this.valueField.SetText("(" + (a_entryObject.position * 100000 | 0) / 100000 + ")");
      }
      this.textField.textColor = BrushListEntry.defaultTextColor;
      if(!this.SliderInstance.initialized)
      {
         this.proxyObject = a_entryObject;
      }
      else
      {
         this.setSlider(this.SliderInstance,a_entryObject);
      }
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc3_;
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
      slider.internalCallback = a_entryObject.internalCallback;
      slider.entryObject = a_entryObject;
      slider.changedCallback = function()
      {
         this.entryObject.position = this.position;
         this._parent.valueField.SetText("(" + (this.position * 1000 | 0) / 1000 + ")");
         if(this.internalCallback)
         {
            this.internalCallback();
         }
      };
      slider.addEventListener("change",slider,"changedCallback");
      slider.disabled = !a_entryObject.enabled;
   }
}
