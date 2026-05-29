class HistoryList extends skyui.components.list.ScrollingList
{
   var _maxScrollPosition;
   var entryList;
   var scrollPosition;
   var selectedIndex;
   var entryHeight = 25;
   function HistoryList()
   {
      super();
   }
   function lastEntry()
   {
      this.scrollPosition = this._maxScrollPosition;
      this.selectedIndex = this.entryList.length - 1;
   }
}
