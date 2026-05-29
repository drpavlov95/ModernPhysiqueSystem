class ColorSlider extends gfx.controls.Slider
{
   var _snapInterval;
   var dispatchEventAndSound;
   var maximum;
   var minimum;
   var value;
   function ColorSlider()
   {
      super();
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
            this.value += 10;
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
            this.value -= 10;
            if(this.value != _loc3_)
            {
               this.dispatchEventAndSound({type:"change"});
            }
         }
      }
      else if(details.navEquivalent === gfx.ui.NavigationCode.GAMEPAD_L1)
      {
         if(_loc4_)
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
         if(details.navEquivalent !== gfx.ui.NavigationCode.GAMEPAD_R1)
         {
            return false;
         }
         if(_loc4_)
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
}
