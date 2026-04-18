typedef enum Coco_Label Coco_Label;

enum Coco_Label {
    Label_person,
    Label_bicycle,
    Label_car,
    Label_motorcycle,
    Label_airplane,
    Label_bus,
    Label_train,
    Label_truck,
    Label_boat,
    Label_traffic_light,
    Label_fire_hydrant,
    Label_stop_sign,
    Label_parking_meter,
    Label_bench,
    Label_bird,
    Label_cat,
    Label_dog,
    Label_horse,
    Label_sheep,
    Label_cow,
    Label_elephant,
    Label_bear,
    Label_zebra,
    Label_giraffe,
    Label_backpack,
    Label_umbrella,
    Label_handbag,
    Label_tie,
    Label_suitcase,
    Label_frisbee,
    Label_skis,
    Label_snowboard,
    Label_sports_ball,
    Label_kite,
    Label_baseball_bat,
    Label_baseball_glove,
    Label_skateboard,
    Label_surfboard,
    Label_tennis_racket,
    Label_bottle,
    Label_wine_glass,
    Label_cup,
    Label_fork,
    Label_knife,
    Label_spoon,
    Label_bowl,
    Label_banana,
    Label_apple,
    Label_sandwich,
    Label_orange,
    Label_broccoli,
    Label_carrot,
    Label_hot_dog,
    Label_pizza,
    Label_donut,
    Label_cake,
    Label_chair,
    Label_couch,
    Label_potted_plant,
    Label_bed,
    Label_dining_table,
    Label_toilet,
    Label_tv,
    Label_laptop,
    Label_mouse,
    Label_remote,
    Label_keyboard,
    Label_cell_phone,
    Label_microwave,
    Label_oven,
    Label_toaster,
    Label_sink,
    Label_refrigerator,
    Label_book,
    Label_clock,
    Label_vase,
    Label_scissors,
    Label_teddy_bear,
    Label_hair_drier,
    Label_toothbrush,
};

const char* label_get_name(Coco_Label label) {
    switch (label) {
        case    Label_person:           return "person";
        case    Label_bicycle:          return "bicycle";
        case    Label_car:              return "car";
        case    Label_motorcycle:       return "motorcycle";
        case    Label_airplane:         return "airplane";
        case    Label_bus:              return "bus";
        case    Label_train:            return "train";
        case    Label_truck:            return "truck";
        case    Label_boat:             return "boat";
        case    Label_traffic_light:    return "traffic_light";
        case    Label_fire_hydrant:     return "fire hydrant";
        case    Label_stop_sign:        return "stop sign";
        case    Label_parking_meter:    return "parking meter";
        case    Label_bench:            return "bench";
        case    Label_bird:             return "bird";
        case    Label_cat:              return "cat";
        case    Label_dog:              return "dog";
        case    Label_horse:            return "horse";
        case    Label_sheep:            return "sheep";
        case    Label_cow:              return "cow";
        case    Label_elephant:         return "elephant";
        case    Label_bear:             return "bear";
        case    Label_zebra:            return "zebra";
        case    Label_giraffe:          return "giraffe";
        case    Label_backpack:         return "backpack";
        case    Label_umbrella:         return "umbrella";
        case    Label_handbag:          return "handbag";
        case    Label_tie:              return "tie";
        case    Label_suitcase:         return "suitcase";
        case    Label_frisbee:          return "frisbee";
        case    Label_skis:             return "skis";
        case    Label_snowboard:        return "snowboard";
        case    Label_sports_ball:      return "sports ball";
        case    Label_kite:             return "kite";
        case    Label_baseball_bat:     return "baseball bat";
        case    Label_baseball_glove:   return "baseball glove";
        case    Label_skateboard:       return "skateboard";
        case    Label_surfboard:        return "surfboard";
        case    Label_tennis_racket:    return "tennis racket";
        case    Label_bottle:           return "bottle";
        case    Label_wine_glass:       return "wine glass";
        case    Label_cup:              return "cup";
        case    Label_fork:             return "fork";
        case    Label_knife:            return "knife";
        case    Label_spoon:            return "spoon";
        case    Label_bowl:             return "bowl";
        case    Label_banana:           return "banana";
        case    Label_apple:            return "apple";
        case    Label_sandwich:         return "sandwich";
        case    Label_orange:           return "orange";
        case    Label_broccoli:         return "broccoli";
        case    Label_carrot:           return "carrot";
        case    Label_hot_dog:          return "hot dog";
        case    Label_pizza:            return "pizza";
        case    Label_donut:            return "donut";
        case    Label_cake:             return "cake";
        case    Label_chair:            return "chair";
        case    Label_couch:            return "couch";
        case    Label_potted_plant:     return "potted plant";
        case    Label_bed:              return "bed";
        case    Label_dining_table:     return "dining table";
        case    Label_toilet:           return "toilet";
        case    Label_tv:               return "tv";
        case    Label_laptop:           return "laptop";
        case    Label_mouse:            return "mouse";
        case    Label_remote:           return "remote";
        case    Label_keyboard:         return "keyboard";
        case    Label_cell_phone:       return "cell phone";
        case    Label_microwave:        return "microwave";
        case    Label_oven:             return "oven";
        case    Label_toaster:          return "toaster";
        case    Label_sink:             return "sink";
        case    Label_refrigerator:     return "refrigerator";
        case    Label_book:             return "book";
        case    Label_clock:            return "clock";
        case    Label_vase:             return "vase";
        case    Label_scissors:         return "scissors";
        case    Label_teddy_bear:       return "teddy bear";
        case    Label_hair_drier:       return "hair drier";
        case    Label_toothbrush:       return "toothbrush";
    }
    return NULL;
}

