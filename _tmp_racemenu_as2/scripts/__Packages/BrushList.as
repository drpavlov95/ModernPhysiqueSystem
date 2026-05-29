class BrushList extends skyui.components.list.ScrollingList
{
   var disableInput;
   var selectedClip;
   var entryHeight = 40;
   function BrushList()
   {
      super();
   }
   function handleInput(details, pathToFocus)
   {
      if(this.disableInput)
      {
         return false;
      }
      var _loc2_ = this.selectedClip != undefined && this.selectedClip.handleInput != undefined && this.selectedClip.handleInput(details,pathToFocus);
      if(_loc2_)
      {
         return true;
      }
      return false;
   }
}
