class MovableWindow extends gfx.core.UIComponent
{
   var _dragOffset;
   var _x;
   var _xmouse;
   var _y;
   var _ymouse;
   var background;
   var dispatchEvent;
   var onMouseMove;
   var onMouseUp;
   function MovableWindow()
   {
      super();
      Mouse.addListener(this);
      gfx.events.EventDispatcher.initialize(this);
      gfx.events.EventDispatcher.initialize(this.background);
   }
   function onLoad()
   {
      super.onLoad();
      this.background.addEventListener("press",this,"beginDrag");
      this.background.onPress = function(controllerIdx, keyboardOrMouse, button)
      {
         if(this.disabled)
         {
            return undefined;
         }
         this.dispatchEvent({type:"press",controllerIdx:controllerIdx,button:button});
      };
   }
   function beginDrag(event)
   {
      this.onMouseMove = this.doDrag;
      this.onMouseUp = this.endDrag;
      this._dragOffset = {x:this._xmouse,y:this._ymouse};
      this.dispatchEvent({type:event.type,controllerIdx:event.controllerIdx,button:event.button});
   }
   function doDrag()
   {
      var _loc3_ = this._xmouse - this._dragOffset.x;
      var _loc2_ = this._ymouse - this._dragOffset.y;
      this._x += _loc3_;
      this._y += _loc2_;
   }
   function endDrag()
   {
      delete this.onMouseUp;
      delete this.onMouseMove;
   }
}
