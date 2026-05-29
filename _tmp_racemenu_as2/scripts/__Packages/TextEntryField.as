class TextEntryField extends MovieClip
{
   var TextInputInstance;
   var _acceptButton;
   var _cancelButton;
   var buttonPanel;
   var dispatchEvent;
   function TextEntryField()
   {
      super();
      gfx.events.EventDispatcher.initialize(this);
   }
   function handleInput(details, pathToFocus)
   {
      var _loc2_ = false;
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.ENTER)
         {
            this.onAccept();
            _loc2_ = true;
         }
         else if(details.navEquivalent == gfx.ui.NavigationCode.TAB)
         {
            this.onCancel();
            _loc2_ = true;
         }
      }
      if(_loc2_)
      {
         return _loc2_;
      }
      var _loc4_ = pathToFocus.shift();
      if(_loc4_.handleInput(details,pathToFocus))
      {
         return true;
      }
      return false;
   }
   function SetupButtons()
   {
      this.buttonPanel.clearButtons();
      var _loc2_ = this.buttonPanel.addButton({text:"$Accept",controls:this._acceptButton});
      var _loc3_ = this.buttonPanel.addButton({text:"$Cancel",controls:this._cancelButton});
      _loc2_.addEventListener("click",this,"onAccept");
      _loc3_.addEventListener("click",this,"onCancel");
      this.buttonPanel.updateButtons(true);
   }
   function updateButtons(bInstant)
   {
      this.buttonPanel.updateButtons(bInstant);
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      if(a_platform == 0)
      {
         this._acceptButton = skyui.defines.Input.Enter;
         this._cancelButton = skyui.defines.Input.Tab;
      }
      else
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = skyui.defines.Input.Cancel;
      }
      this.buttonPanel.setPlatform(a_platform,a_bPS3Switch);
   }
   function GetValidName()
   {
      return this.TextInputInstance.text.length > 0;
   }
   function onAccept()
   {
      if(this.GetValidName())
      {
         this.dispatchEvent({type:"nameChange",nameChanged:true});
      }
   }
   function onCancel()
   {
      this.dispatchEvent({type:"nameChange",nameChanged:false});
   }
}
