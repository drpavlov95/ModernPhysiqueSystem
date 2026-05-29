class TextCategoryButtons extends MovieClip
{
   var background;
   var buttonLeft;
   var buttonRight;
   var categoryList;
   var triggerLeft;
   var triggerRight;
   function TextCategoryButtons()
   {
      super();
      this.categoryList = this._parent.slidingCategoryList.categoryList;
      Mouse.addListener(this);
      this.buttonRight._alpha = this.buttonLeft._alpha = 40;
      gfx.events.EventDispatcher.initialize(this);
      this.triggerLeft.onRollOver = function()
      {
         this._parent.buttonLeft._alpha = 100;
      };
      this.triggerLeft.onRollOut = function()
      {
         this._parent.buttonLeft._alpha = 40;
      };
      this.triggerLeft.onReleaseOutside = this.triggerLeft.onRollOut;
      this.triggerLeft.onPress = function()
      {
         this._parent.dispatchEvent({type:"pressLeft"});
      };
      this.triggerRight.onRollOver = function()
      {
         this._parent.buttonRight._alpha = 100;
      };
      this.triggerRight.onRollOut = function()
      {
         this._parent.buttonRight._alpha = 40;
      };
      this.triggerRight.onReleaseOutside = this.triggerRight.onRollOut;
      this.triggerRight.onPress = function()
      {
         this._parent.dispatchEvent({type:"pressRight"});
      };
   }
   function onMouseWheel(a_delta)
   {
      if(this.categoryList.disableInput)
      {
         return undefined;
      }
      var _loc2_ = Mouse.getTopMostEntity();
      while(_loc2_ && _loc2_ != undefined)
      {
         if(_loc2_ == this || _loc2_ == this.categoryList || _loc2_ == this.categoryList.background || _loc2_ == this.background)
         {
            if(a_delta < 0)
            {
               this.categoryList.moveSelectionRight(false);
               break;
            }
            this.categoryList.moveSelectionLeft(false);
            break;
         }
         _loc2_ = _loc2_._parent;
      }
   }
}
