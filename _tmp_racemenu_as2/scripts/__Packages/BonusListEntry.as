class BonusListEntry extends skyui.components.list.BasicListEntry
{
   var textField;
   var valueField;
   function BonusListEntry()
   {
      super();
   }
   function setEntry(a_entryObject, a_state)
   {
      this.textField.SetText(!a_entryObject.text ? " " : a_entryObject.text);
      this.valueField.SetText((a_entryObject.value * 100 | 0) / 100);
   }
}
