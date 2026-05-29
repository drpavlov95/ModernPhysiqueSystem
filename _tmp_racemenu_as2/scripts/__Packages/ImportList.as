class ImportList extends skyui.components.list.ScrollingList
{
   var _entryList;
   var disableSelection;
   var dispatchEvent;
   var listState;
   var selectedIndex;
   var entryHeight = 25;
   function ImportList()
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
   function moveSelectionUp(a_bScrollPage)
   {
      var _loc3_;
      var _loc4_;
      if(!this.disableSelection)
      {
         if(this.listState.focusEntry != undefined && this.listState.focusEntry >= 0)
         {
            _loc3_ = this.listState.focusEntry - 1;
            if(_loc3_ >= 0)
            {
               _loc4_ = this.entryList[_loc3_];
               this.entryList[_loc3_] = this.entryList[this.listState.focusEntry];
               this.entryList[this.listState.focusEntry] = _loc4_;
               this.listState.focusEntry = _loc3_;
               this.InvalidateData();
               this.selectedIndex = this.listState.focusEntry;
               this.dispatchEvent({type:"shifted",index:this.listState.focusEntry});
               return undefined;
            }
         }
      }
      super.moveSelectionUp(a_bScrollPage);
   }
   function moveSelectionDown(a_bScrollPage)
   {
      var _loc3_;
      var _loc4_;
      if(!this.disableSelection)
      {
         if(this.listState.focusEntry != undefined && this.listState.focusEntry >= 0)
         {
            _loc3_ = this.listState.focusEntry + 1;
            if(_loc3_ < this.entryList.length)
            {
               _loc4_ = this.entryList[_loc3_];
               this.entryList[_loc3_] = this.entryList[this.listState.focusEntry];
               this.entryList[this.listState.focusEntry] = _loc4_;
               this.listState.focusEntry = _loc3_;
               this.InvalidateData();
               this.selectedIndex = this.listState.focusEntry;
               this.dispatchEvent({type:"shifted",index:this.listState.focusEntry});
               return undefined;
            }
         }
      }
      super.moveSelectionDown(a_bScrollPage);
   }
}
