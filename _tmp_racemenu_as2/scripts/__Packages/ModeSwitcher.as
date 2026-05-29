class ModeSwitcher extends MovieClip
{
   var Lock;
   var _modes;
   var buttonGroup;
   var dispatchEvent;
   var tabContainer;
   var _offset = 5;
   var _padding = 25;
   function ModeSwitcher()
   {
      super();
      this._modes = new Array();
      Shared.GlobalFunc.SetLockFunction();
      gfx.events.EventDispatcher.initialize(this);
   }
   function InitExtensions()
   {
      this.Lock("R");
      var _loc3_;
      if(_global.skse.plugins.CharGen.bEnableSculpting)
      {
         _loc3_ = this.addMode("$Sculpt");
         this.buttonGroup.addButton(_loc3_);
         _loc3_.addEventListener("rollOver",this,"onItemRollOver");
      }
   }
   function onLoad()
   {
      this.tabContainer = this.createEmptyMovieClip("tabContainer",this.getNextHighestDepth());
      this.buttonGroup = new gfx.controls.ButtonGroup("tabs",this.tabContainer);
      var _loc2_ = this.addMode("$Sliders");
      var _loc4_ = this.addMode("$Presets");
      var _loc3_ = this.addMode("$Camera");
      this.buttonGroup.addButton(_loc2_);
      this.buttonGroup.addButton(_loc4_);
      this.buttonGroup.addButton(_loc3_);
      this.buttonGroup.setSelectedButton(_loc2_);
      _loc2_.addEventListener("rollOver",this,"onItemRollOver");
      _loc4_.addEventListener("rollOver",this,"onItemRollOver");
      _loc3_.addEventListener("rollOver",this,"onItemRollOver");
      this.buttonGroup.addEventListener("change",this,"onItemChanged");
   }
   function handleInput(details, pathToFocus)
   {
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_R2)
         {
            this.nextCategory();
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.GAMEPAD_L2)
         {
            this.previousCategory();
            return true;
         }
      }
      return false;
   }
   function nextCategory()
   {
      var _loc2_ = this.buttonGroup.indexOf(this.buttonGroup.selectedButton);
      _loc2_ += 1;
      if(_loc2_ >= this.buttonGroup.length)
      {
         _loc2_ = 0;
      }
      this.buttonGroup.setSelectedButton(this.buttonGroup.getButtonAt(_loc2_));
   }
   function previousCategory()
   {
      var _loc2_ = this.buttonGroup.indexOf(this.buttonGroup.selectedButton);
      _loc2_ -= 1;
      if(_loc2_ < 0)
      {
         _loc2_ = this.buttonGroup.length - 1;
      }
      this.buttonGroup.setSelectedButton(this.buttonGroup.getButtonAt(_loc2_));
   }
   function setMode(index)
   {
      var _loc2_ = this.buttonGroup.getButtonAt(index);
      if(_loc2_.enabled)
      {
         this.buttonGroup.setSelectedButton(_loc2_);
      }
   }
   function updateAlignment()
   {
      this.tabContainer._x = - this.tabContainer._width - this._offset;
   }
   function getMode()
   {
      return this._modes.indexOf(this.buttonGroup.selectedButton);
   }
   function addMode(a_text)
   {
      var _loc3_ = this.tabContainer.attachMovie("ModeTab","tab" + this._modes,this.tabContainer.getNextHighestDepth());
      _loc3_.tab.textField.text = a_text;
      _loc3_.tab.textField.autoSize = "center";
      _loc3_.tab.background._width = _loc3_.tab.textField._width + this._padding * 2;
      var _loc4_ = 0;
      var _loc2_ = 0;
      while(_loc2_ < this._modes.length)
      {
         _loc4_ += this._modes[_loc2_].tab.background._width + this._offset;
         _loc2_ = _loc2_ + 1;
      }
      _loc3_._x = _loc4_ + _loc3_.tab.background._width / 2;
      this._modes.push(_loc3_);
      this.updateAlignment();
      return _loc3_;
   }
   function onItemRollOver(event)
   {
      this.dispatchEvent({type:"tabRollOver",index:this._modes.indexOf(event.target),item:event.target});
   }
   function onItemChanged(event)
   {
      this.dispatchEvent({type:"changeMode",index:this._modes.indexOf(event.item),item:event.item});
   }
}
