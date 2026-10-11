/* Adapt DAB's converted-level scale to GoldenEye's native room files.
 * gen_stagetable.py divides the Bean/native fit by GoldenEye's level scale;
 * bgSetScale() stores that same reciprocal in room_data_float2. */
static float beanRoomFileScale(float convertedScale, float renderScale)
{
    return isfinite(convertedScale) && isfinite(renderScale) && convertedScale > 0 && renderScale > 0
        ? convertedScale / renderScale : 0;
}
