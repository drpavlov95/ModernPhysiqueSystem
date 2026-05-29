class HistoryListEntry extends MovieClip
{
   var itemIndex;
   var selectIndicator;
   var textField;
   var trigger;
   static var defaultTextColor = 16777215;
   static var disabledTextColor = 5000268;
   function HistoryListEntry()
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
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc3_ = a_entryObject == a_state.list.selectedEntry;
      if(this.textField != undefined)
      {
         this.textField.autoSize = !a_entryObject.align ? "left" : a_entryObject.align;
         this.textField.SetText(!a_entryObject.text ? " " : a_entryObject.text);
      }
      if(this.itemIndex > a_state.list.historyIndex)
      {
         this.textField.textColor = HistoryListEntry.disabledTextColor;
      }
      else
      {
         this.textField.textColor = HistoryListEntry.defaultTextColor;
      }
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc3_;
      }
   }
}
