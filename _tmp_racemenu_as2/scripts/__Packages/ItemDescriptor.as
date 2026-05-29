class ItemDescriptor extends MovieClip
{
   var background;
   var textField;
   var fadeDelay = 1;
   function ItemDescriptor()
   {
      super();
      this.textField.autoSize = "left";
   }
   function setText(a_text, a_fadeDelay)
   {
      this.textField.text = a_text;
      this.background._width = this.textField._x + this.textField.textWidth + 20;
      if(a_fadeDelay)
      {
         this.fadeDelay = a_fadeDelay;
      }
      else
      {
         this.fadeDelay = 1;
      }
   }
   function fadeOut()
   {
      com.greensock.TweenLite.to(this,1,{delay:this.fadeDelay,autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
   }
   function toggle(a_toggle)
   {
      this._visible = this.enabled = a_toggle;
      com.greensock.TweenLite.killTweensOf(this,false,{_alpha:true,autoAlpha:true});
      if(a_toggle)
      {
         this._alpha = 100;
      }
      else
      {
         this._alpha = 0;
      }
   }
}
