class BrushCategoryList extends TextCategoryList
{
   var disableInput;
   var moveSelectionLeft;
   var moveSelectionRight;
   function BrushCategoryList()
   {
      super();
   }
   function handleInput(details, pathToFocus)
   {
      if(this.disableInput)
      {
         return false;
      }
      if(Shared.GlobalFunc.IsKeyPressed(details))
      {
         if(details.navEquivalent == gfx.ui.NavigationCode.UP)
         {
            this.moveSelectionLeft(true);
            return true;
         }
         if(details.navEquivalent == gfx.ui.NavigationCode.DOWN)
         {
            this.moveSelectionRight(true);
            return true;
         }
      }
      return false;
   }
}
