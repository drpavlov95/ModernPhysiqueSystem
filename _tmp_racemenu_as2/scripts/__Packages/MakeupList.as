class MakeupList extends skyui.components.list.ScrollingList
{
   var _entryList;
   var _listHeight;
   var _maxListIndex;
   var background;
   var scrollbar;
   var entryHeight = 25;
   function MakeupList()
   {
      super();
   }
   function set entryList(a_list)
   {
      this._entryList = a_list;
   }
   function set listHeight(a_height)
   {
      this._listHeight = this.background._height = a_height;
      if(this.scrollbar != undefined)
      {
         this.scrollbar.height = this._listHeight;
      }
      this._maxListIndex = Math.floor(this._listHeight / this.entryHeight);
   }
}
