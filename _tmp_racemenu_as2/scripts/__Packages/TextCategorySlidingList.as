class TextCategorySlidingList extends MovieClip
{
   var categoryList;
   function TextCategorySlidingList()
   {
      super();
      gfx.events.EventDispatcher.initialize(this);
      this.categoryList.onInvalidate = function()
      {
         this._parent.updatePosition();
      };
   }
   function onLoad()
   {
      super.onLoad();
      this.categoryList.addEventListener("selectionChange",this,"onCategoryChange");
   }
   function updatePosition()
   {
      var _loc2_ = this.categoryList.getClipData();
      com.greensock.TweenLite.to(this.categoryList,0.25,{_x:this.categoryList.background._width / 2 - _loc2_[1] / 2 - _loc2_[0],overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
   }
   function onCategoryChange(event)
   {
      this.updatePosition();
   }
}
