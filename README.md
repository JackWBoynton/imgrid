## ImGrid


an ImGui Dynamic Grid system.

![image](https://github.com/user-attachments/assets/ca6b70aa-037e-4fd9-8558-2b032ad56fd7)

Based on [gridstack](https://github.com/gridstack/gridstack.js) but written in ImGui.
Uses a lot of boilerplate from [ImNodes](https://github.com/Nelarius/imnodes).

Widgets can be dragged by their title bars, and other widgets will automatically reposition to fill the gaps. There are still a few issues with the layout, but it's mostly working.

There are also still some issues with sizing of content within their allocated widgets

### Minimal Example

```cpp
ImGrid::CreateContext();
...
if (ImGui::Begin("Grid")) {
  ImGrid::BeginGrid();
  {
    ImGrid::BeginEntry(0);
    {
      ImGrid::BeginEntryTitleBar();
      ImGui::Text("Entry 0");
      ImGrid::EndEntryTitleBar();

      ImGui::Text("Content");
    }
    ImGrid::EndEntry();
  }
  ImGrid::EndGrid();
  }
ImGui::End();
```
A more detailed example can be found here [example](example/main.cpp).
