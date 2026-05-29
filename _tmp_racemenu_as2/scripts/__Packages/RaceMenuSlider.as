class RaceMenuSlider extends gfx.controls.Slider
{
   var __width;
   var _maximum;
   var _minimum;
   var _snapInterval;
   var _value;
   var dispatchEvent;
   var dispatchEventAndSound;
   var maximum;
   var minimum;
   var offsetLeft;
   var offsetRight;
   var thumb;
   var value;
   function RaceMenuSlider()
   {
      super();
   }
   function onLoad()
   {
      super.onLoad();
      this.dispatchEvent({type:"onWidgetLoad"});
   }
   function handleInput(details, pathToFocus)
   {
      var _loc3_ = this.value;
      var _loc4_ = details.value == "keyDown" || details.value == "keyHold";
      if(details.navEquivalent === gfx.ui.NavigationCode.RIGHT)
      {
         if(_loc4_)
         {
            this.value += this._snapInterval;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else if(details.navEquivalent === gfx.ui.NavigationCode.LEFT)
      {
         if(_loc4_)
         {
            this.value -= this._snapInterval;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else if(details.navEquivalent === gfx.ui.NavigationCode.GAMEPAD_R2)
      {
         if(_loc4_)
         {
            this.value += this.maximum / 8;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else if(details.navEquivalent === gfx.ui.NavigationCode.GAMEPAD_L2)
      {
         if(_loc4_)
         {
            this.value -= this.maximum / 8;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else if(details.navEquivalent === gfx.ui.NavigationCode.HOME)
      {
         if(!_loc4_)
         {
            this.value = this.minimum;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else
      {
         if(details.navEquivalent !== gfx.ui.NavigationCode.END)
         {
            return false;
         }
         if(!_loc4_)
         {
            this.value = this.maximum;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      return true;
   }
   function scrollWheel(delta)
   {
   }
   function updateThumb()
   {
      var _loc2_ = this.__width - this.offsetLeft - this.offsetRight;
      this.thumb._x = (this._value - this._minimum) / (this._maximum - this._minimum) * _loc2_ - this.thumb._width / 2 + this.offsetLeft;
   }
}
