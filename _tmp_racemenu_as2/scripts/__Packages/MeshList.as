class MeshList extends skyui.components.list.ScrollingList
{
   var _entryList;
   var _selectedIndex;
   var disableInput;
   var disableSelection;
   var dispatchEvent;
   var isListAnimating;
   var selectedClip;
   var selectedEntry;
   var entryHeight = 25;
   function MeshList()
   {
      super();
   }
   function set entryList(a_list)
   {
      this._entryList = a_list;
   }
   function get entryList()
   {
      return this._entryList;
   }
   function onItemPressVisibility(a_index)
   {
      if(this.disableInput || this.disableSelection || this._selectedIndex == -1)
      {
         return undefined;
      }
      this.dispatchEvent({type:"itemPressVisibility",index:this._selectedIndex,entry:this.selectedEntry,clip:this.selectedClip,keyboardOrMouse:skyui.components.list.BasicList.SELECT_KEYBOARD});
   }
   function onItemPressWireframe(a_index)
   {
      if(this.disableInput || this.disableSelection || this._selectedIndex == -1)
      {
         return undefined;
      }
      this.dispatchEvent({type:"itemPressWireframe",index:this._selectedIndex,entry:this.selectedEntry,clip:this.selectedClip,keyboardOrMouse:skyui.components.list.BasicList.SELECT_KEYBOARD});
   }
   function onItemPressLock(a_index)
   {
      if(this.disableInput || this.disableSelection || this._selectedIndex == -1)
      {
         return undefined;
      }
      this.dispatchEvent({type:"itemPressLock",index:this._selectedIndex,entry:this.selectedEntry,clip:this.selectedClip,keyboardOrMouse:skyui.components.list.BasicList.SELECT_KEYBOARD});
   }
   function onStatusRollOver(a_index, a_type)
   {
      if(this.isListAnimating || this.disableSelection || this.disableInput)
      {
         return undefined;
      }
      this.dispatchEvent({type:"statusRollOver",index:a_index,status:a_type});
   }
}
