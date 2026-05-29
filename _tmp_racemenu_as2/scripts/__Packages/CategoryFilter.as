class CategoryFilter implements skyui.filter.IFilter
{
   var _matcherFunc;
   var dispatchEvent;
   var _itemFilter = 4294967295;
   var _textFilter = "all";
   function CategoryFilter()
   {
      gfx.events.EventDispatcher.initialize(this);
      this._matcherFunc = this.entryMatchesFilter;
   }
   function get itemFilter()
   {
      return this._itemFilter;
   }
   function get textFilter()
   {
      return this._textFilter;
   }
   function changeFilterData(a_newFilter, a_newTextFilter, a_bDoNotUpdate)
   {
      if(a_bDoNotUpdate == undefined)
      {
         a_bDoNotUpdate = false;
      }
      this._itemFilter = a_newFilter;
      this._textFilter = a_newTextFilter;
      if(!a_bDoNotUpdate)
      {
         this.dispatchEvent({type:"filterChange"});
      }
   }
   function setPartitionedFilterMode(a_bPartition)
   {
      this._matcherFunc = !a_bPartition ? this.entryMatchesFilter : this.entryMatchesPartitionedFilter;
   }
   function applyFilter(a_filteredList)
   {
      var _loc2_ = 0;
      while(_loc2_ < a_filteredList.length)
      {
         if(!this._matcherFunc(a_filteredList[_loc2_]))
         {
            a_filteredList.splice(_loc2_,1);
            _loc2_ = _loc2_ - 1;
         }
         _loc2_ = _loc2_ + 1;
      }
   }
   function entryMatchesFilter(a_entry)
   {
      return a_entry != undefined && (a_entry.filterFlag == undefined || (a_entry.filterFlag & this._itemFilter) != 0) || a_entry.textFilters && this.matchText(a_entry.textFilters);
   }
   function matchText(a_filters)
   {
      var _loc2_;
      if(a_filters)
      {
         _loc2_ = 0;
         while(_loc2_ < a_filters.length)
         {
            if(a_filters[_loc2_] == this._textFilter)
            {
               return true;
            }
            _loc2_ = _loc2_ + 1;
         }
      }
      return false;
   }
   function entryMatchesPartitionedFilter(a_entry)
   {
      var _loc2_ = false;
      var _loc3_;
      if(a_entry != undefined)
      {
         if(this._itemFilter == 4294967295 || this._textFilter == "all")
         {
            _loc2_ = true;
         }
         else
         {
            _loc3_ = a_entry.filterFlag;
            _loc2_ = (_loc3_ & 0xFF) == this._itemFilter || (_loc3_ & 0xFF00) >>> 8 == this._itemFilter || (_loc3_ & 0xFF0000) >>> 16 == this._itemFilter || (_loc3_ & 0xFF000000) >>> 24 == this._itemFilter;
            if(!_loc2_)
            {
               _loc2_ = this.matchText(a_entry.textFilters);
            }
         }
      }
      return _loc2_;
   }
}
