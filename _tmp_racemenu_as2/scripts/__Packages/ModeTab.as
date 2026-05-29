class ModeTab extends gfx.controls.RadioButton
{
   var addEventListener;
   var tab;
   var textField;
   function ModeTab()
   {
      super();
      this.textField = this.tab.textField;
   }
   function onLoad()
   {
      super.onLoad();
      this.addEventListener("rollOver",this,"onTabRollOver");
      this.addEventListener("rollOut",this,"onTabRollOut");
      this.addEventListener("releaseOutside",this,"onTabRollOut");
   }
   function onTabRollOver(event)
   {
      com.greensock.TweenLite.to(this.tab,0.5,{_y:22,overwrite:com.greensock.OverwriteManager.AUTO,easing:com.greensock.easing.Linear.easeNone});
   }
   function onTabRollOut(event)
   {
      com.greensock.TweenLite.to(this.tab,0.5,{_y:0,overwrite:com.greensock.OverwriteManager.AUTO,easing:com.greensock.easing.Linear.easeNone});
   }
}
