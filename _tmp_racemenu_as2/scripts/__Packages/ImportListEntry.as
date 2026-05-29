class ImportListEntry extends skyui.components.list.BasicListEntry
{
   var checkbox;
   var focusIndicator;
   var selectIndicator;
   var textField;
   var valueField;
   static var validTextColor = 1611029;
   static var mismatchTextColor = 16711680;
   static var invalidTextColor = 5000268;
   function ImportListEntry()
   {
      super();
      this.textField.textAutoSize = "shrink";
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc10_ = a_entryObject == a_state.list.selectedEntry;
      var _loc9_ = a_entryObject.itemIndex == a_state.focusEntry;
      var _loc11_ = a_state.focusEntry != undefined && a_state.focusEntry != -1;
      var _loc3_ = a_state.pair.entryList[a_entryObject.itemIndex];
      var _loc4_ = _loc3_ && (a_entryObject.morphable == true || _loc3_.morphable == true);
      var _loc7_ = _loc3_ && (a_entryObject.unused == true || _loc3_.unused == true);
      var _loc5_ = _loc3_ && (a_entryObject.disabled == true || _loc3_.disabled == true);
      var _loc8_ = _loc3_.vertices == a_entryObject.vertices;
      if(_loc8_ && _loc4_ && !_loc5_)
      {
         this.textField.textColor = ImportListEntry.validTextColor;
         this.valueField.textColor = ImportListEntry.validTextColor;
      }
      else if(!_loc4_ || _loc7_ || _loc5_)
      {
         this.textField.textColor = ImportListEntry.invalidTextColor;
         this.valueField.textColor = ImportListEntry.invalidTextColor;
      }
      else if(!_loc8_)
      {
         this.textField.textColor = ImportListEntry.mismatchTextColor;
         this.valueField.textColor = ImportListEntry.mismatchTextColor;
      }
      this.checkbox.gotoAndStop(!(_loc5_ || !_loc4_ || _loc7_) ? "checked" : "unchecked");
      if(!_loc4_ || _loc7_)
      {
         this.checkbox._alpha = 40;
      }
      else
      {
         this.checkbox._alpha = 100;
      }
      this.textField.SetText(!a_entryObject.name ? " " : a_entryObject.name);
      this.valueField.SetText((a_entryObject.vertices * 100 | 0) / 100);
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc10_ && !_loc9_;
      }
      if(this.focusIndicator != undefined)
      {
         this.focusIndicator._visible = _loc9_;
      }
   }
}
