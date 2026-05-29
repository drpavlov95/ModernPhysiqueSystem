class FileViewerDialog extends BasicTweenDialog
{
   var _acceptButton;
   var _bPS3Switch;
   var _backButton;
   var _cancelButton;
   var _platform;
   var _rootPath;
   var _sortFilter;
   var _textButton;
   var background;
   var buttonPanel;
   var currentLevel;
   var defaultText;
   var disableInput;
   var dispatchEvent;
   var fileInput;
   var fileList;
   var path;
   var pathTextField;
   var patterns;
   var titleText;
   var titleTextField;
   var alpha = 30;
   var bTextEntryMode = false;
   function FileViewerDialog()
   {
      super();
      this.background.onRollOver = this.background.onRollOut = this.background.onPress = this.background.onRelease = function()
      {
      };
      this.background._alpha = this.alpha;
      this._sortFilter = new skyui.filter.SortFilter();
   }
   function onLoad()
   {
      super.onLoad();
      this.pathTextField.SetText(this.path);
      this.titleTextField.SetText(this.titleText);
      this.fileInput.text = this.defaultText;
      this.fileInput.isDisabled = this.disableInput;
      this.fileInput.addEventListener("inputStart",this,"onInputStart");
      this.fileInput.addEventListener("inputEnd",this,"onInputEnd");
      this.SetPlatform(this._platform,this._bPS3Switch);
      this._sortFilter.setSortBy(["directory","name"],[Array.DESCENDING | Array.NUMERIC,Array.NUMERIC]);
      this._rootPath = this.path;
      this.currentLevel = {path:this.path,parent:null};
      this.fileList.addEventListener("itemPress",this,"onItemPress");
      this.fileList.addEventListener("selectionChange",this,"onSelectionChange");
      this.fileList.entryList = _global.skse.plugins.CharGen.GetExternalFiles(this.path,this.patterns);
      var _loc4_ = new skyui.components.list.FilteredEnumeration(this.fileList.entryList);
      _loc4_.addFilter(this._sortFilter);
      this.fileList.listEnumeration = _loc4_;
      this.fileList.requestInvalidate();
   }
   function onInputStart()
   {
      this.bTextEntryMode = true;
   }
   function onInputEnd()
   {
      this.bTextEntryMode = false;
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      if(this.bTextEntryMode)
      {
         if(Shared.GlobalFunc.IsKeyPressed(details))
         {
            if(details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.navEquivalent == gfx.ui.NavigationCode.TAB)
            {
               this.fileInput.endInput();
               return true;
            }
            if(this._textButton && this.IsBoundKeyPressed(details,this._textButton,this._platform))
            {
               this.fileInput.endInput();
               return true;
            }
         }
         return true;
      }
      if(this.fileList.handleInput(details,pathToFocus) && this.fileList.selectedEntry)
      {
         return true;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
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
         if(this.IsBoundKeyPressed(details,this._backButton,this._platform))
         {
            this.onBackPress();
            return true;
         }
         if(this._textButton && this.IsBoundKeyPressed(details,this._textButton,this._platform))
         {
            this.fileInput.startInput();
            return true;
         }
      }
      return true;
   }
   function updatePathText()
   {
      this.pathTextField.SetText(this.currentLevel.path);
   }
   function onBackPress()
   {
      if(this.currentLevel.parent)
      {
         this.currentLevel = this.currentLevel.parent;
         this.loadPath(this.currentLevel.path);
         this.updatePathText();
         this.updateButtons();
      }
   }
   function loadPath(a_path)
   {
      var _loc4_ = _global.skse.plugins.CharGen.GetExternalFiles(a_path,this.patterns);
      this.fileList.entryList.splice(0,this.fileList.entryList.length);
      var _loc3_ = 0;
      while(_loc3_ < _loc4_.length)
      {
         this.fileList.entryList.push(_loc4_[_loc3_]);
         _loc3_ = _loc3_ + 1;
      }
      this.fileList.requestInvalidate();
   }
   function onItemPress(event)
   {
      var _loc2_ = this.fileList.entryList[event.index];
      if(_loc2_.directory)
      {
         this.currentLevel = {path:_loc2_.path,parent:this.currentLevel};
         this.loadPath(this.currentLevel.path);
         this.updatePathText();
         this.updateButtons();
      }
      else
      {
         if(_loc2_)
         {
            this.fileInput.text = _loc2_.name;
         }
         this.onAcceptPress();
      }
   }
   function onSelectionChange(event)
   {
      var _loc2_ = this.fileList.entryList[event.index];
      if(_loc2_)
      {
         if(this.fileInput.isDisabled)
         {
            this.fileInput.text = _loc2_.name;
         }
         this.dispatchEvent({type:"selectionChanged",directoryPath:this.currentLevel.path,input:this.fileInput.text,directory:_loc2_.directory});
      }
   }
   function onAcceptPress()
   {
      this.dispatchEvent({type:"accept",directoryPath:this.currentLevel.path,input:this.fileInput.text});
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
         this._backButton = skyui.defines.Input.Wait;
         this._textButton = undefined;
      }
      else
      {
         this._acceptButton = skyui.defines.Input.Accept;
         this._cancelButton = skyui.defines.Input.Cancel;
         this._backButton = skyui.defines.Input.Wait;
         this._textButton = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Jump",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      }
      this.updateButtons();
   }
   function updateButtons()
   {
      this.buttonPanel.clearButtons();
      var _loc2_ = this.buttonPanel.addButton({text:"$Accept",controls:this._acceptButton});
      _loc2_.addEventListener("press",this,"onAcceptPress");
      var _loc4_;
      if(this.currentLevel.parent)
      {
         _loc4_ = this.buttonPanel.addButton({text:"$Back",controls:this._backButton});
         _loc4_.addEventListener("press",this,"onBackPress");
      }
      if(this._textButton)
      {
         this.buttonPanel.addButton({text:"$Edit",controls:this._textButton});
      }
      var _loc3_ = this.buttonPanel.addButton({text:"$Cancel",controls:this._cancelButton});
      _loc3_.addEventListener("press",this,"onCancelPress");
      this.buttonPanel.updateButtons();
   }
}
