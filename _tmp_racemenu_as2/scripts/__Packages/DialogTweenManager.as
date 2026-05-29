class DialogTweenManager
{
   static var _SetPlatform;
   static var _activeDialog;
   static var _handleInput;
   static var _previousFocus;
   static var _target;
   function DialogTweenManager()
   {
   }
   static function open(a_target, a_linkageID, a_init)
   {
      if(DialogTweenManager._activeDialog)
      {
         DialogTweenManager.close();
      }
      DialogTweenManager._previousFocus = gfx.managers.FocusHandler.instance.getFocus(0);
      DialogTweenManager._activeDialog = BasicTweenDialog(a_target.attachMovie(a_linkageID,"dialog",a_target.getNextHighestDepth(),a_init));
      gfx.managers.FocusHandler.instance.setFocus(DialogTweenManager._activeDialog,0);
      DialogTweenManager._target = a_target;
      DialogTweenManager._handleInput = a_target.handleInput;
      DialogTweenManager._SetPlatform = a_target.SetPlatform;
      a_target.SetPlatform = function(a_platform, a_bPS3Switch)
      {
         DialogTweenManager._SetPlatform(a_platform,a_bPS3Switch);
         DialogTweenManager._activeDialog.SetPlatform(a_platform,a_bPS3Switch);
      };
      DialogTweenManager._activeDialog.openDialog();
      return DialogTweenManager._activeDialog;
   }
   static function close()
   {
      gfx.managers.FocusHandler.instance.setFocus(DialogTweenManager._previousFocus,0);
      DialogTweenManager._target.handleInput = DialogTweenManager._handleInput;
      DialogTweenManager._target.SetPlatform = DialogTweenManager._SetPlatform;
      DialogTweenManager._handleInput = null;
      DialogTweenManager._SetPlatform = null;
      DialogTweenManager._target = null;
      DialogTweenManager._activeDialog.closeDialog();
      DialogTweenManager._activeDialog = null;
   }
}
