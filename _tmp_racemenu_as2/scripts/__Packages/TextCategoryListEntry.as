class TextCategoryListEntry extends skyui.components.list.BasicListEntry
{
   var _alpha;
   var background;
   var enabled;
   var textField;
   function TextCategoryListEntry()
   {
      super();
   }
   function initialize(a_index, a_state)
   {
      super.initialize();
   }
   function setEntry(a_entryObject, a_state)
   {
      this.textField.SetText("");
      this.textField._width = 1;
      this.textField.autoSize = "left";
      this.textField.SetText(a_entryObject.text);
      this.background._width = this.textField._width;
      if(a_entryObject.filterFlag == 0 && !a_entryObject.bDontHide)
      {
         this._alpha = 15;
         this.enabled = false;
      }
      else if(a_entryObject == a_state.list.selectedEntry)
      {
         this._alpha = 100;
         this.enabled = true;
      }
      else
      {
         this._alpha = 50;
         this.enabled = true;
      }
   }
}
