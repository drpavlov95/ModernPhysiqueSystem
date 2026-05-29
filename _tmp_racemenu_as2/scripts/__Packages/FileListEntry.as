class FileListEntry extends MovieClip
{
   var dateField;
   var selectIndicator;
   var sizeField;
   var textField;
   var trigger;
   function FileListEntry()
   {
      super();
      this.textField.textAutoSize = "shrink";
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
   function doubleDigit(a_number)
   {
      return String(Math.floor(a_number / 10)) + String(Math.floor(a_number % 10));
   }
   function formatDate(a_date)
   {
      return this.doubleDigit(a_date.getDate()) + "/" + this.doubleDigit(a_date.getMonth() + 1) + "/" + a_date.getFullYear() + " " + this.doubleDigit(a_date.getHours()) + ":" + this.doubleDigit(a_date.getMinutes()) + ":" + this.doubleDigit(a_date.getSeconds());
   }
   function formatSize(a_size)
   {
      return Math.ceil(a_size / 1000) + " KB";
   }
   function setEntry(a_entryObject, a_state)
   {
      var _loc4_ = a_entryObject == a_state.list.selectedEntry;
      if(this.textField != undefined)
      {
         this.textField.autoSize = !a_entryObject.align ? "left" : a_entryObject.align;
         this.textField.SetText(!a_entryObject.name ? " " : a_entryObject.name);
      }
      if(this.dateField != undefined)
      {
         if(!a_entryObject.formattedDate)
         {
            a_entryObject.formattedDate = this.formatDate(a_entryObject.lastModified);
         }
         this.dateField.SetText(a_entryObject.formattedDate);
      }
      var _loc3_;
      if(this.sizeField != undefined)
      {
         _loc3_ = this.sizeField.getTextFormat();
         if(!a_entryObject.directory)
         {
            if(!a_entryObject.formattedSize)
            {
               a_entryObject.formattedSize = this.formatSize(a_entryObject.size);
            }
            _loc3_.align = "right";
            this.sizeField.SetText(a_entryObject.formattedSize);
         }
         else
         {
            _loc3_.align = "center";
            this.sizeField.SetText("-");
         }
         this.sizeField.setTextFormat(_loc3_);
      }
      if(this.selectIndicator != undefined)
      {
         this.selectIndicator._visible = _loc4_;
      }
   }
}
