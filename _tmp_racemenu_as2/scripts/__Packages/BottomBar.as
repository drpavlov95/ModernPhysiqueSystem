class BottomBar extends MovieClip
{
   var background;
   var buttonPanel;
   var playerInfo;
   static var SKYUI_RELEASE_IDX = 17;
   static var SKYUI_VERSION_MAJOR = 5;
   static var SKYUI_VERSION_MINOR = 0;
   static var SKYUI_VERSION_STRING = BottomBar.SKYUI_VERSION_MAJOR + "." + BottomBar.SKYUI_VERSION_MINOR;
   function BottomBar()
   {
      super();
   }
   function positionElements(a_leftOffset, a_rightOffset)
   {
      this.buttonPanel._x = a_leftOffset;
      this.buttonPanel.updateButtons(true);
   }
   function positionBackground()
   {
      var _loc2_ = {x:Stage.visibleRect.x,y:Stage.visibleRect.y};
      this.globalToLocal(_loc2_);
      this.background._xscale = Stage.width / 1280 * 100;
      this.background._x = _loc2_.x;
   }
   function showPlayerInfo()
   {
      this.playerInfo._alpha = 100;
   }
   function hidePlayerInfo()
   {
      this.playerInfo._alpha = 0;
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      Stage.scaleMode = "showAll";
      this.buttonPanel.setPlatform(a_platform,a_bPS3Switch);
   }
}
