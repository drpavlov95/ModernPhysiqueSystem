class MeshListEntry extends MovieClip
{
   var lockToggle;
   var selectIndicator;
   var textField;
   var trigger;
   var visibleToggle;
   var wireColor;
   var wireToggle;
   function MeshListEntry()
   {
      super();
      this.trigger.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
         }
      };
      this.trigger.onRollOut = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOut(this._parent.itemIndex);
         }
      };
      this.trigger.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPress(this._parent.itemIndex);
         }
      };
      this.visibleToggle.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
            _loc2_.onStatusRollOver(this._parent.itemIndex,0);
         }
      };
      this.visibleToggle.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPressVisibility(this._parent.itemIndex);
         }
      };
      this.wireToggle.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
            _loc2_.onStatusRollOver(this._parent.itemIndex,1);
         }
      };
      this.wireToggle.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPressWireframe(this._parent.itemIndex);
         }
      };
      this.lockToggle.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
            _loc2_.onStatusRollOver(this._parent.itemIndex,2);
         }
      };
      this.lockToggle.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPressLock(this._parent.itemIndex);
         }
      };
      this.wireColor.onRollOver = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemRollOver(this._parent.itemIndex);
            _loc2_.onStatusRollOver(this._parent.itemIndex,3);
         }
      };
      this.wireColor.onPress = function()
      {
         var _loc2_ = this._parent._parent;
         if(this._parent.itemIndex != undefined && this.enabled)
         {
            _loc2_.onItemPressAux(this._parent.itemIndex,undefined,1);
         }
      };
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc4_ = a_entryObject == a_state.list.selectedEntry;
      this.visibleToggle.gotoAndStop(!a_entryObject.visible ? "unchecked" : "checked");
      this.wireToggle.gotoAndStop(!a_entryObject.wireframe ? "unchecked" : "checked");
      this.lockToggle.gotoAndStop(!a_entryObject.locked ? "checked" : "unchecked");
      var _loc3_ = new Color(this.wireColor.fill);
      _loc3_.setRGB(a_entryObject.wfColor & 0xFFFFFF);
      this.wireColor.fill._alpha = (a_entryObject.wfColor >>> 24) / 255 * 100;
      if(this.textField != undefined)
      {
         this.textField.autoSize = !a_entryObject.align ? "left" : a_entryObject.align;
         this.textField.SetText(!a_entryObject.name ? " " : a_entryObject.name);
      }
      if(!a_entryObject.morphable)
      {
         this.lockToggle.enabled = false;
         this.lockToggle._alpha = 40;
      }
      else
      {
         this.lockToggle.enabled = true;
         this.lockToggle._alpha = 100;
      }
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc4_;
      }
   }
}
