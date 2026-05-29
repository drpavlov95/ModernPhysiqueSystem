class ImportDialog extends BasicTweenDialog
{
   var _acceptButton;
   var _bPS3Switch;
   var _cancelButton;
   var _platform;
   var _selectButton;
   var _toggleButton;
   var background;
   var buttonPanel;
   var destination;
   var dispatchEvent;
   var importList;
   var importStaticList;
   var source;
   var titleText;
   var titleTextField;
   var alpha = 30;
   function ImportDialog()
   {
      super();
      this.background.onRollOver = this.background.onRollOut = this.background.onPress = this.background.onRelease = function()
      {
      };
      this.background._alpha = this.alpha;
   }
   function onLoad()
   {
      super.onLoad();
      this.titleTextField.SetText(this.titleText);
      var _loc7_;
      var _loc3_;
      if(this.source.length < this.destination.length)
      {
         _loc7_ = this.destination.length - this.source.length;
         _loc3_ = 0;
         while(_loc3_ < _loc7_)
         {
            this.source.push({name:"$Undefined",vertices:0,unused:true});
            _loc3_ = _loc3_ + 1;
         }
      }
      this.matchParts(this.source,this.destination);
      this.importList.entryList = this.source;
      this.importStaticList.entryList = this.destination;
      this.importList.listState.pair = this.importStaticList;
      this.importStaticList.listState.pair = this.importList;
      this.importList.listEnumeration = new skyui.components.list.BasicEnumeration(this.importList.entryList);
      this.importStaticList.listEnumeration = new skyui.components.list.BasicEnumeration(this.importStaticList.entryList);
      this.importStaticList.disableInput = true;
      this.importList.addEventListener("selectionChange",this,"onSelectionChange");
      this.importList.addEventListener("itemPress",this,"onItemPress");
      this.importList.addEventListener("shifted",this,"onItemShifted");
      this.importList.requestInvalidate();
      this.importStaticList.requestInvalidate();
      this.SetPlatform(this._platform,this._bPS3Switch);
   }
   function matchParts(a_source, a_destination)
   {
      var _loc4_ = new Array(Math.max(a_source.length,a_destination.length));
      var _loc3_ = new Array(Math.max(a_source.length,a_destination.length));
      var _loc6_ = new Array(Math.max(a_source.length,a_destination.length));
      var _loc8_ = 0;
      while(_loc8_ < a_source.length)
      {
         _loc3_[_loc8_] = a_source[_loc8_];
         _loc8_ = _loc8_ + 1;
      }
      _loc8_ = 0;
      while(_loc8_ < a_destination.length)
      {
         _loc6_[_loc8_] = a_destination[_loc8_];
         _loc8_ = _loc8_ + 1;
      }
      _loc8_ = _loc3_.length - 1;
      var _loc1_;
      while(_loc8_ >= 0)
      {
         _loc1_ = _loc6_.length - 1;
         while(_loc1_ >= 0)
         {
            if(_loc3_[_loc8_].vertices == _loc6_[_loc1_].vertices)
            {
               _loc4_[_loc1_] = a_source[_loc8_];
               _loc3_.splice(_loc8_,1);
               _loc6_[_loc1_] = undefined;
               break;
            }
            _loc1_ = _loc1_ - 1;
         }
         _loc8_ = _loc8_ - 1;
      }
      var _loc5_ = 0;
      var _loc2_;
      while(_loc5_ < _loc3_.length)
      {
         _loc2_ = 0;
         while(_loc2_ < _loc4_.length)
         {
            if(_loc4_[_loc2_] == undefined && _loc3_[_loc5_] != undefined)
            {
               _loc4_[_loc2_] = _loc3_[_loc5_];
               break;
            }
            _loc2_ = _loc2_ + 1;
         }
         _loc5_ = _loc5_ + 1;
      }
      _loc8_ = 0;
      while(_loc8_ < a_source.length)
      {
         a_source[_loc8_] = _loc4_[_loc8_];
         _loc8_ = _loc8_ + 1;
      }
      false;
      false;
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function onItemPress(event)
   {
      this.importList.listState.focusEntry = event.index;
      this.importStaticList.listState.focusEntry = event.index;
      this.importList.requestUpdate();
      this.importStaticList.requestUpdate();
      this.updateButtons();
   }
   function onSelectionChange(event)
   {
      this.importStaticList.selectedIndex = event.index;
      this.importStaticList.scrollPosition = this.importList.scrollPosition;
      if(this.importStaticList.listState.focusEntry != this.importList.listState.focusEntry)
      {
         this.importStaticList.listState.focusEntry = this.importList.listState.focusEntry;
         this.importStaticList.requestUpdate();
      }
   }
   function onItemShifted(event)
   {
      this.importStaticList.selectedIndex = event.index;
      this.importStaticList.scrollPosition = this.importList.scrollPosition;
      if(this.importStaticList.listState.focusEntry != this.importList.listState.focusEntry)
      {
         this.importStaticList.listState.focusEntry = this.importList.listState.focusEntry;
         this.importStaticList.requestUpdate();
      }
   }
   function onSelectionPress(event)
   {
      var _loc2_ = this.importList.listState.focusEntry != undefined && this.importList.listState.focusEntry >= 0;
      if(_loc2_)
      {
         this.importList.selectedIndex = this.importList.listState.focusEntry;
         this.importStaticList.selectedIndex = this.importStaticList.listState.focusEntry;
         this.importList.listState.focusEntry = undefined;
         this.importStaticList.listState.focusEntry = undefined;
      }
      else
      {
         this.importList.listState.focusEntry = this.importList.selectedIndex;
         this.importStaticList.listState.focusEntry = this.importList.selectedIndex;
      }
      this.importList.requestUpdate();
      this.importStaticList.requestUpdate();
      this.updateButtons();
   }
   function onTogglePress(event)
   {
      var _loc3_ = this.importList.selectedEntry;
      var _loc2_ = this.importStaticList.selectedEntry;
      var _loc4_ = _loc2_ && (_loc3_.morphable == true || _loc2_.morphable == true);
      var _loc5_ = _loc2_ && (_loc3_.unused == true || _loc2_.unused == true);
      if(_loc4_ && !_loc5_)
      {
         if(this.importList.selectedEntry.disabled == undefined)
         {
            this.importList.selectedEntry.disabled = true;
         }
         else
         {
            this.importList.selectedEntry.disabled = !this.importList.selectedEntry.disabled;
         }
         this.importList.requestUpdate();
         this.importStaticList.requestUpdate();
      }
   }
   function handleInput(details, pathToFocus)
   {
      if(Shared.GlobalFunc.IsKeyPressed(details,false))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Activate",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onAcceptPress();
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.TAB || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Cancel",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0))
         {
            this.onCancelPress();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._selectButton,this._platform))
         {
            this.onSelectionPress();
            return true;
         }
         if(this.IsBoundKeyPressed(details,this._toggleButton,this._platform))
         {
            this.onTogglePress();
            return true;
         }
      }
      if(this.importList.handleInput(details,pathToFocus))
      {
         return true;
      }
      return true;
   }
   function onDialogClosed()
   {
      _global.skse.plugins.CharGen.ReleaseImportedHead();
   }
   function onAcceptPress()
   {
      var _loc9_ = new Array();
      var _loc4_ = 0;
      var _loc3_;
      var _loc2_;
      var _loc7_;
      var _loc8_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < this.importStaticList.entryList.length)
      {
         _loc3_ = this.importList.entryList[_loc4_];
         _loc2_ = this.importStaticList.entryList[_loc4_];
         _loc7_ = _loc2_ && (_loc3_.morphable == true || _loc2_.morphable == true);
         _loc8_ = _loc2_ && (_loc3_.unused == true || _loc2_.unused == true);
         _loc6_ = _loc2_ && (_loc3_.disabled == true || _loc2_.disabled == true);
         _loc5_ = _loc2_.vertices == _loc3_.vertices;
         _loc9_.push(!(_loc7_ && _loc5_ && !_loc8_ && !_loc6_) ? -1 : _loc3_.gIndex);
         _loc4_ = _loc4_ + 1;
      }
      this.dispatchEvent({type:"accept",matches:_loc9_});
      DialogTweenManager.close();
   }
   function onCancelPress()
   {
      DialogTweenManager.close();
   }
   function SetPlatform(a_platform, a_bPS3Switch)
   {
      this.buttonPanel.setPlatform(a_platform,a_bPS3Switch);
      this._platform = a_platform;
      if(this._platform == 0)
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = {name:"Tween Menu",context:skyui.defines.Input.CONTEXT_GAMEPLAY};
         this._toggleButton = skyui.defines.Input.Wait;
         this._selectButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Toggle POV",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      else
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = skyui.defines.Input.Cancel;
         this._toggleButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Jump",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
         this._selectButton = skyui.defines.Input.Wait;
      }
      this.updateButtons();
   }
   function updateButtons()
   {
      this.buttonPanel.clearButtons();
      var _loc3_ = this.buttonPanel.addButton({text:"$Accept",controls:this._acceptButton});
      _loc3_.addEventListener("press",this,"onAcceptPress");
      var _loc4_ = this.buttonPanel.addButton({text:"$Cancel",controls:this._cancelButton});
      _loc4_.addEventListener("press",this,"onCancelPress");
      var _loc6_ = this.buttonPanel.addButton({text:"$Toggle",controls:this._toggleButton});
      _loc6_.addEventListener("press",this,"onTogglePress");
      var _loc5_ = this.importList.listState.focusEntry != undefined && this.importList.listState.focusEntry >= 0;
      var _loc2_ = this.buttonPanel.addButton({text:(!_loc5_ ? "$Select" : "$Deselect"),controls:this._selectButton});
      _loc2_.addEventListener("press",this,"onSelectionPress");
      this.buttonPanel.updateButtons();
   }
}
