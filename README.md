int main() {
    Canvas canvas;
    initCanvas(&canvas, 10, 10);
    int choice;
    int x, y;
    char symbol, filename[100];
    while (1) {
        printf("\n====================================\n");
        printf("       C PIXEL ART EDITOR           \n");
        printf("====================================\n");
        displayCanvas(&canvas);
        printf("\n--- TOOLBOX MENU ---\n");
        printf("1. Draw Pixel\n");
        printf("2. Erase Pixel\n");
        printf("3. Fill Region (Flood Fill)\n");
        printf("4. Save Canvas to File\n");
        printf("5. Load Canvas from File\n");
        printf("6. Reset / Clear Canvas\n");
        printf("7. Exit\n");
        printf("Select a tool (1-7): ");
        if (scanf("%d", &choice) != 1) {
            clearInputBuffer();
            printf("Invalid input. Please enter a valid number.\n");
            continue;
        }
        switch (choice) {
            case 1:
                printf("Enter coordinates (X Y): ");
                if (scanf("%d %d", &x, &y) == 2) {
                    printf("Enter brush character (e.g., #, *, @, O): ");
                    clearInputBuffer();
                    scanf("%c", &symbol);
                    drawPixel(&canvas, x, y, symbol);
                } else {
                    printf("Invalid coordinates!\n");
                    clearInputBuffer();
                }
                break;
            case 2:
                printf("Enter coordinates to erase (X Y): ");
                if (scanf("%d %d", &x, &y) == 2) {
                    erasePixel(&canvas, x, y);
                } else {
                    printf("Invalid coordinates!\n");
                    clearInputBuffer();
                }
                break;
            case 3:
                printf("Enter target coordinates to fill from (X Y): ");
                if (scanf("%d %d", &x, &y) == 2) {
                    if (x >= 0 && x < canvas.width && y >= 0 && y < canvas.height) {
                        char target = canvas.grid[y][x];
                        printf("Enter fill character: ");
                        clearInputBuffer();
                        scanf("%c", &symbol);
                        floodFill(&canvas, x, y, target, symbol);
                        printf("Region filled successfully!\n");
                    } else {
                        printf("Coordinates out of bounds!\n");
                    }
                } else {
                    printf("Invalid coordinates!\n");
                    clearInputBuffer();
                }
                break;
            case 4:
                printf("Enter filename to save (e.g., my_art.txt): ");
                scanf("%s", filename);
                if (saveToFile(&canvas, filename)) {
                    printf("Artwork successfully saved to '%s'!\n", filename);
                } else {
                    printf("Failed to save artwork.\n");
                }
                break;
            case 5:
                printf("Enter filename to load: ");
                scanf("%s", filename);
                if (loadFromFile(&canvas, filename)) {
                    printf("Artwork successfully loaded from '%s'!\n", filename);
                } else {
                    printf("Failed to load artwork.\n");
                }
                break;
            case 6:
                initCanvas(&canvas, canvas.width, canvas.height);
                printf("Canvas reset to default state.\n");
                break;
            case 7:
                printf("Exiting Pixel Art Editor. Happy drawing!\n");
                return 0;
            default:
                printf("Invalid choice! Choose an option between 1 and 7.\n");
        }
    }
    return 0;
}
